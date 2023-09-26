/* Copyright (c) 2013-14 Codethink Ltd. (http://www.codethink.co.uk)
 *
 * This file is part of frepo.
 *
 * frepo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * frepo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with frepo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <semaphore.h>

#include "git.h"
#include "xml.h"
#include "path.h"
#include "manifest.h"
#include "settings.h"


typedef enum
{
	frepo_command_init,
	frepo_command_sync,
	frepo_command_snapshot,
	frepo_command_list,
	frepo_command_forall,
	frepo_command_count
} frepo_command_e;



void print_usage(const char* prog)
{
	printf("%s init name -u manifest [-b branch] [-g groups] [--mirror] [-j threads]\n", prog);
	printf("%s sync [-f] [-b branch] [-g groups] [-j threads]\n", prog);
	printf("%s snapshot name [-g groups]\n", prog);
	printf("%s list [-g groups]\n", prog);
	printf("%s forall  [-g groups] [-p] -c command\n", prog);
}


struct manifest_thread_params
{
	const char* manifest_url;
	manifest_t* manifest;
	bool        mirror;
	sem_t*      semaphore;
	unsigned    retries;
	unsigned    retry_delay;

	pthread_t   thread;
	unsigned    project;
	bool*       error;
	bool        complete;
};

static void* frepo_sync_manifest__thread(void* param)
{
	volatile struct manifest_thread_params* tp
		= (volatile struct manifest_thread_params*)param;

	unsigned p = tp->project;

	bool exists = git_exists(tp->manifest->project[p].path);

	printf("%s repository (%u/%u) '%s'.\n",
		(exists ? "Updating" : "Cloning"),
		(p + 1), tp->manifest->project_count,
		tp->manifest->project[p].path);

	char* revision = NULL;
	bool revision_differs = false;

	if (exists && !tp->mirror)
	{
		revision = git_current_branch(
			tp->manifest->project[p].path);
		if (!revision)
		{
			fprintf(stderr, "Error: Failed to check current revision of '%s'.\n",
				tp->manifest->project[p].path);
			*(tp->error) = true;
			sem_post(tp->semaphore);
			return NULL;
		}

		revision_differs
			= (strcmp(revision, tp->manifest->project[p].revision) != 0);
		if (revision_differs && !git_checkout(
			tp->manifest->project[p].path,
			tp->manifest->project[p].revision, false))
		{
			free(revision);
			fprintf(stderr, "Error: Failed to checkout revision '%s' of '%s'.\n",
				tp->manifest->project[p].revision,
				tp->manifest->project[p].path);
			*(tp->error) = true;
			sem_post(tp->semaphore);
			return NULL;
		}
	}

	char* remote_full
		= path_join(tp->manifest_url,
			tp->manifest->project[p].remote);
	if (!remote_full)
	{
		fprintf(stderr,
			"Error: Failed to create relative repo url"
				", since manifest url is unknown.");
		*(tp->error) = true;
		sem_post(tp->semaphore);
		return NULL;
	}

	bool update_success = git_update(
		tp->manifest->project[p].path,
		remote_full,
		tp->manifest->project[p].name,
		tp->manifest->project[p].remote_name,
		tp->manifest->project[p].revision, tp->mirror);

	unsigned r, d;
	for (r = 0, d = tp->retry_delay;
		!update_success && (r < tp->retries);
		r++, d *= 2)
	{
		fprintf(stderr, "Warning: Failed to %s '%s'"
			", waiting %u ms and retrying.\n",
			(exists ? "update" : "clone"),
			tp->manifest->project[p].path, d);

		usleep(tp->retry_delay * 1000);

		update_success = git_update(
			tp->manifest->project[p].path,
			remote_full,
			tp->manifest->project[p].name,
			tp->manifest->project[p].remote_name,
			tp->manifest->project[p].revision, tp->mirror);
	}

	if (!update_success)
	{
		fprintf(stderr, "Error: Failed to %s '%s'",
			(exists ? "update" : "clone"),
			tp->manifest->project[p].path);
		if (tp->retries != 0)
			fprintf(stderr, " after %u retries", tp->retries);
		fprintf(stderr, ".\n");
		*(tp->error) = true;
	}
	free(remote_full);

	unsigned j;
	for (j = 0; j < tp->manifest->project[p].copyfile_count; j++)
	{
		char cmd[strlen(tp->manifest->project[p].path)
			+ strlen(tp->manifest->project[p].copyfile[j].source)
			+ strlen(tp->manifest->project[p].copyfile[j].dest) + 16];
		sprintf(cmd, "cp %s/%s %s",
			tp->manifest->project[p].path,
			tp->manifest->project[p].copyfile[j].source,
			tp->manifest->project[p].copyfile[j].dest);
		if (system(cmd) != EXIT_SUCCESS)
		{
			unsigned k;
			for (k = 0; k < j; k++)
				git_remove(tp->manifest->project[k].path);
			fprintf(stderr,
				"Error: Failed to perform copy '%s' to '%s'"
				" for project '%s'\n",
				tp->manifest->project[p].copyfile[j].source,
				tp->manifest->project[p].copyfile[j].dest,
				tp->manifest->project[p].path);
			*(tp->error) = true;
		}
	}

	if (revision_differs && !git_checkout(
		tp->manifest->project[p].path,
		revision, false))
	{
		fprintf(stderr, "Error: Failed to revert '%s' to revision '%s'.\n",
			tp->manifest->project[p].path, revision);
		*(tp->error) = true;
	}

	free(revision);

	tp->complete = true;
	sem_post(tp->semaphore);
	return NULL;
}

static bool frepo_sync_manifest(
	manifest_t* manifest, const char* url,
	bool mirror, long int threads)
{
	if (!manifest)
		return false;

	if (manifest->project_count == 0)
		return true;

	if (threads <= 0)
		threads = 1;
	if ((unsigned)threads > manifest->project_count)
		threads = manifest->project_count;

	sem_t semaphore;
	if (sem_init(&semaphore, 0, 0) < 0)
		abort();

	volatile struct manifest_thread_params tp[threads];
	long int i;
	for (i = 0; i < threads; i++)
	{
		tp[i].manifest_url = url;
		tp[i].manifest     = manifest;
		tp[i].mirror       = mirror;
		tp[i].semaphore    = &semaphore;
		tp[i].retries      = 8;
		tp[i].retry_delay  = 100;
	}

	unsigned p = 0;
	unsigned error_count = 0;
	bool error[manifest->project_count];

	long int t;
	for (t = 0; t < threads; t++)
	{
		error[p] = false;
		tp[t].project  = p;
		tp[t].error    = &error[p];
		tp[t].complete = false;

		if (pthread_create(
			(void*)&tp[t].thread, NULL,
			frepo_sync_manifest__thread,
			(void*)&tp[t]) != 0)
			abort();
		p++;
	}

	while (p < manifest->project_count)
	{
		sem_wait(&semaphore);

		do
		{
			for (t = 0; t < threads; t++)
			{
				if (*tp[t].error || tp[t].complete)
				{
					error_count += (*tp[t].error ? 1 : 0);
					break;
				}
			}
		} while (t >= threads);

		error[p] = false;
		tp[t].project  = p;
		tp[t].error    = &error[p];
		tp[t].complete = false;

		if (pthread_create(
			(void*)&tp[t].thread, NULL,
			frepo_sync_manifest__thread,
			(void*)&tp[t]) != 0)
			abort();
		p++;
	}

	for (t = 0; t < threads; t++)
		sem_wait(&semaphore);
	sem_destroy(&semaphore);

	for (t = 0; t < threads; t++)
	{
		if (tp[t].error || tp[t].complete)
			error_count += (*tp[t].error ? 1 : 0);
	}

	if (error_count != 0)
	{
		for (p = 0; p < manifest->project_count; p++)
		{
			if (error[p])
				fprintf(stderr, "Error: Failed to sync project '%s'.\n",
					manifest->project[p].path);
		}
		return false;
	}

	return true;
}

static int frepo_init(
	manifest_t* manifest, const char* url,
	bool mirror, long int threads)
{
	return (frepo_sync_manifest(manifest, url, mirror, threads)
		? EXIT_SUCCESS : EXIT_FAILURE);
}

static int frepo_sync(
	manifest_t* manifest,
	const char* manifest_repo,
	const char* manifest_path,
	const char* manifest_url,
	bool force, const char* branch,
	group_t* group, unsigned group_count,
	long int threads)
{
	char* manifest_branch = NULL;
	char* manifest_branch_old = NULL;
	char* manifest_head_old = NULL;
	char* manifest_head_latest = NULL;
	manifest_t* manifest_updated = NULL;
	manifest_t* manifest_old = NULL;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
	{
		fprintf(stderr, "Error: Failed to store parent directory.\n");
		goto frepo_sync_failed;
	}

	bool manifest_uncommitted_changes;
	if (!git_uncomitted_changes(manifest_repo, &manifest_uncommitted_changes))
	{
		fprintf(stderr, "Error: Failed to check for uncommitted changes"
			" in your manifest.\n");
		goto frepo_sync_failed;
	}
	else if (manifest_uncommitted_changes)
	{
		fprintf(stderr, "Error: There are uncommitted changes in your manifest,"
			" commit or discard these to continue.\n");
		goto frepo_sync_failed;
	}

	manifest_head_old
		= git_current_commit(manifest_repo);
	if (!manifest_head_old)
	{
		fprintf(stderr, "Error: Failed to get local manifest commit.\n");
		goto frepo_sync_failed;
	}

	printf("Updating manifest.\n");

	manifest_branch = git_current_branch(manifest_repo);
	if (branch)
	{
		manifest_branch_old = manifest_branch;
		if (!git_checkout(manifest_repo, branch, false))
		{
			fprintf(stderr, "Error: Failed to checkout manifest branch.\n");
			goto frepo_sync_failed;
		}
		manifest_branch = (char*)branch;
	}

	if (!git_update(manifest_repo,
			NULL, NULL, NULL,
			manifest_branch, false))
	{
		fprintf(stderr, "Error: Failed to update manifest.\n");
		goto frepo_sync_failed;
	}

	manifest_head_latest
		= git_current_commit(manifest_repo);
	if (!manifest_head_latest)
	{
		fprintf(stderr, "Error: Failed to get latest manifest commit.\n");
		goto frepo_sync_failed;
	}

	bool manifest_changed
		= (strcmp(manifest_head_old, manifest_head_latest) != 0);

	if (manifest_changed)
	{
		manifest_updated = manifest_read(manifest_path);
		if (!manifest_updated)
		{
			fprintf(stderr, "Error: Failed to read new manifest.\n");
			goto frepo_sync_failed;
		}

		manifest_t* manifest_filtered
			= manifest_group_filter(manifest_updated, group, group_count);
		if (!manifest_filtered)
		{
			fprintf(stderr, "Error: Failed to filter new manifest.\n");
			goto frepo_sync_failed;
		}
		manifest_filtered->document = manifest_updated->document;
		manifest_updated->document = NULL;
		manifest_delete(manifest_updated);
		manifest_updated = manifest_filtered;

		manifest_old = manifest_subtract(
			manifest, manifest_updated);
		if (manifest_old && (manifest_old->project_count > 0))
		{
			if (!force)
			{
				fprintf(stderr,
					"Changes to the manifest require repositories"
					" to be deleted. You must sync with the force"
					" flag '-f' to delete repositories.\n");
				goto frepo_sync_failed;
			}

			printf("%u repositories will be removed.\n",
				manifest_old->project_count);

			unsigned i;
			for (i = 0; i < manifest_old->project_count; i++)
			{
				bool uncommitted_changes;
				if (!git_uncomitted_changes(
					manifest_old->project[i].path, &uncommitted_changes))
				{
					fprintf(stderr, "Error: '%s' is deprecated but can't remove"
						" because checking for uncommitted changes failed.\n",
						manifest_old->project[i].name);
					goto frepo_sync_failed;
				}
				else if (uncommitted_changes)
				{
					fprintf(stderr, "Error: '%s' is deprecated but can't remove"
						" because it has uncommitted changes.\n",
						manifest_old->project[i].name);
					goto frepo_sync_failed;
				}
			}
		}
	}
	else
	{
		manifest_updated = manifest_copy(manifest);
	}

	if (manifest_updated)
	{
		unsigned i;
		for (i = 0; i < manifest_updated->project_count; i++)
		{
			printf("Checking for uncommitted changes in '%s' (%u/%u).\n",
				manifest_updated->project[i].path,
				(i + 1), manifest_updated->project_count);

			if (!git_exists(manifest_updated->project[i].path))
				continue;

			bool uncommitted_changes;
			if (!git_uncomitted_changes(
				manifest_updated->project[i].path, &uncommitted_changes))
			{
				fprintf(stderr, "Error: Failed to check for uncommitted changes"
					" in  '%s', won't update.\n",
						manifest_updated->project[i].name);
				goto frepo_sync_failed;
			}
			else if (uncommitted_changes)
			{
				fprintf(stderr, "Error: '%s' has uncommitted changes"
					", won't update.\n", manifest_updated->project[i].name);
				goto frepo_sync_failed;
			}
		}

		if (!frepo_sync_manifest(manifest_updated, manifest_url, false, threads))
			return EXIT_FAILURE;
	}

	if (manifest_old)
	{
		unsigned i;
		for (i = 0; i < manifest_old->project_count; i++)
		{
			printf("Removing old repository (%u/%u) '%s'.\n",
				(i + 1), manifest_old->project_count,
				manifest_old->project[i].path);

			if (!git_remove(manifest_old->project[i].path))
				fprintf(stderr, "Warning: Failed to remove deprecated"
					" project '%s'.\n", manifest_old->project[i].path);
		}
	}

	/* TODO - Delete unused directories. */

	manifest_delete(manifest_updated);
	manifest_delete(manifest_old);
	free(manifest_head_latest);
	free(manifest_head_old);
	free(manifest_branch_old);
	if (manifest_branch != branch)
		free(manifest_branch);
	return EXIT_SUCCESS;

frepo_sync_failed:
	if (manifest_branch_old)
		git_checkout(manifest_repo, manifest_branch_old, false);
	if (manifest_head_old)
		git_reset_hard(manifest_repo, manifest_head_old);
	manifest_delete(manifest_updated);
	manifest_delete(manifest_old);
	free(manifest_head_latest);
	free(manifest_head_old);
	free(manifest_branch_old);
	if (manifest_branch != branch)
		free(manifest_branch);
	return EXIT_FAILURE;
}

static int frepo_snapshot(
	manifest_t* manifest,
	const char* manifest_repo,
	const char* manifest_path,
	const char* name)
{
	bool changes;
	if (!git_uncomitted_changes(manifest_repo, &changes))
	{
		fprintf(stderr, "Error: Failed to check for uncommitted changes to manifest.\n");
		return EXIT_FAILURE;
	}

	if (changes)
	{
		fprintf(stderr, "Error: Can't snapshot a manifest repository with uncommitted changes.\n");
		return EXIT_FAILURE;
	}

	char* branch = git_current_branch(manifest_repo);
	if (!branch)
		branch = git_current_commit(manifest_repo);

	if (!branch)
	{
		fprintf(stderr, "Error: Failed get current HEAD.\n");
		return EXIT_FAILURE;
	}

	if (!git_checkout(manifest_repo, name, true))
	{
		free(branch);
		fprintf(stderr, "Error: Failed to checkout snapshot branch '%s'.\n", name);
		return EXIT_FAILURE;
	}

	char snapshot_message[64 + strlen(name)];
	sprintf(snapshot_message, "Manifest snapshot '%s'", name);

	if (!manifest_write_snapshot(manifest, manifest_path))
	{
		fprintf(stderr, "Error: Failed to snapshot manifest.\n");
		goto frepo_snapshot_failed;
	}

	if (!git_commit(manifest_repo, snapshot_message))
	{
		fprintf(stderr, "Error: Failed to commit snapshot, reverting.\n");
		if (!git_reset_hard(manifest_repo, "HEAD"))
			fprintf(stderr, "Warning: Failed to reset uncommitted snapshot.\n");
		goto frepo_snapshot_failed;
	}

	if (!git_checkout(manifest_repo, branch, false))
		fprintf(stderr, "Warning: Failed to revert manifest"
			" to previous branch '%s'.\n", branch);
	free(branch);

	return EXIT_SUCCESS;

frepo_snapshot_failed:
	if (!git_checkout(manifest_repo, branch, false))
		fprintf(stderr, "Warning: Failed to revert manifest"
			" to previous branch '%s'.\n", branch);
	else if (!git_reset_hard(manifest_repo, "HEAD"))
		fprintf(stderr, "Warning: Failed to reset manifest to HEAD.\n");
	free(branch);
	return EXIT_FAILURE;
}

static int frepo_list(manifest_t* manifest)
{
	unsigned i;
	for (i = 0; i < manifest->project_count; i++)
		printf("%s : %s\n",
			manifest->project[i].path,
			manifest->project[i].name);
	return EXIT_SUCCESS;
}

static int frepo_forall(manifest_t* manifest, int argc, char** argv, bool print)
{
	if (!manifest)
		return EXIT_FAILURE;

	if ((argc <= 0) || !argv)
	{
		fprintf(stderr, "Error: No argument supplied to forall.\n");
		return EXIT_FAILURE;
	}

	int i, cmd_len;
	for (i = 0, cmd_len = 0; i < argc; i++)
		cmd_len += strlen(argv[i]) + 1;

	char cmd[cmd_len];
	strcpy(cmd, argv[0]);
	
	for (i = 1; i < argc; i++)
	{
		strcat(cmd, " ");
		strcat(cmd, argv[i]);
	}

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
	{
		fprintf(stderr, "Error: Failed to get current directory.\n");
		return EXIT_FAILURE;
	}

	unsigned j;
	for (j = 0; j < manifest->project_count; j++)
	{
		if (print)
			printf("project %s\n", manifest->project[j].path);

		setenv("REPO_PROJECT", manifest->project[j].name, 1);
		setenv("REPO_PATH", manifest->project[j].path, 1);

		if (manifest->project[j].remote_name)
			setenv("REPO_REMOTE", manifest->project[j].remote_name, 1);
		else
			unsetenv("REPO_REMOTE");

		if (manifest->project[j].revision)
			setenv("REPO_RREV", manifest->project[j].revision, 1);
		else
			unsetenv("REPO_RREV");

		/* TODO - Set REMOTE_LREV to HEAD HASH */
		unsetenv("REPO_LREV");

		if (chdir(manifest->project[j].path) != 0)
		{
			fprintf(stderr, "Error: Failed to enter directory '%s'.\n",
				manifest->project[j].path);
			return EXIT_FAILURE;
		}

		if (system(cmd) != EXIT_SUCCESS)
		{
			/* Do nothing. */
		}

		if (chdir(pdir) != 0)
		{
			fprintf(stderr, "Error: Failed to exit directory '%s'.\n",
				manifest->project[j].path);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}



int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "Error: No command given.\n");
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	frepo_command_e command;
	if (strcmp(argv[1], "init") == 0)
		command = frepo_command_init;
	else if (strcmp(argv[1], "sync") == 0)
		command = frepo_command_sync;
	else if (strcmp(argv[1], "snapshot") == 0)
		command = frepo_command_snapshot;
	else if (strcmp(argv[1], "list") == 0)
		command = frepo_command_list;
	else if (strcmp(argv[1], "forall") == 0)
		command = frepo_command_forall;
	else
	{
		fprintf(stderr, "Error: Invalid command '%s'.\n", argv[1]);
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	const char* name    = NULL;
	const char* repo    = NULL;
	const char* branch  = NULL;
	bool        force   = false;
	bool        print   = false;
	long int    threads = 0;

	const char* settings_path = ".frepo/config.ini";
	settings_t* settings = settings_read(settings_path);
	if (!settings)
	{
		settings = settings_create(false);
		if (!settings)
		{
			fprintf(stderr, "Error: Failed to initialize settings.\n");
			return EXIT_FAILURE;
		}
	}

	int    fa_argc = 0;
	char** fa_argv = NULL;

	int a = 2;
	if ((command == frepo_command_init)
		|| (command == frepo_command_snapshot))
	{
		if (argc < 3)
		{
			fprintf(stderr, "Error: No name given.\n");
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
		name = argv[a++];
	}

	for (; a < argc; a++)
	{
		if (argv[a][0] != '-')
		{
			fprintf(stderr, "Error: Unexpected argument '%s'.\n", argv[a]);
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}

		if (argv[a][1] != '-')
		{
			if (argv[a][2] != '\0')
			{
				fprintf(stderr,
					"Error: Malformed flag in argument '%s'.\n", argv[a]);
				print_usage(argv[0]);
				return EXIT_FAILURE;
			}

			switch (argv[a][1])
			{
				case 'u':
					if (command != frepo_command_init)
					{
						fprintf(stderr,
							"Error: -u flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					if ((a + 1) >= argc)
					{
						fprintf(stderr,
							"Error: No url supplied with manifest url flag.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					repo = argv[++a];
					break;
				case 'b':
					if ((command != frepo_command_init)
						&& (command != frepo_command_sync))
					{
						fprintf(stderr,
							"Error: -b flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					if ((a + 1) >= argc)
					{
						fprintf(stderr,
							"Error: No branch supplied with branch flag.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					branch = argv[++a];
					break;
				case 'g':
					if ((a + 1) >= argc)
					{
						fprintf(stderr,
							"Error: No groups supplied with group flag.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					if (!group_list_parse(
						argv[++a], true,
						&settings->group,
						&settings->group_count))
					{
						fprintf(stderr,
							"Error: Failed to parse groups.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				case 'c':
					if (command != frepo_command_forall)
					{
						fprintf(stderr,
							"Error: -c flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					fa_argc = (argc - (a + 1));
					fa_argv = &argv[a + 1];
					a = (argc - 1);
					break;
				case 'p':
					if (command != frepo_command_forall)
					{
						fprintf(stderr,
							"Error: -p flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					print = true;
					break;
				case 'f':
					if (command != frepo_command_sync)
					{
						fprintf(stderr,
							"Error: -f flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					force = true;
					break;
				case 'j':
					if ((command != frepo_command_init)
						&& (command != frepo_command_sync))
					{
						fprintf(stderr,
							"Error: -j flag invalid for command.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}

					threads = strtol(argv[++a], NULL, 0);
					if (threads <= 0)
					{
						fprintf(stderr,
							"Error: Invalid number of threads '%s'.\n", argv[a]);
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				default:
					fprintf(stderr,
						"Error: Invalid flag '%s'.\n", argv[a]);
					print_usage(argv[0]);
					return EXIT_FAILURE;
			}
		}
		else
		{
			if (strcmp(argv[a], "--mirror") == 0)
			{
				if (command != frepo_command_init)
				{
					fprintf(stderr,
						"Error: --mirror flag invalid for command.\n");
					print_usage(argv[0]);
					return EXIT_FAILURE;
				}
				settings->mirror = true;
			}
			else
			{
				fprintf(stderr,
					"Error: Invalid flag '%s'.\n", argv[a]);
				print_usage(argv[0]);
				return EXIT_FAILURE;
			}
		}
	}

	if (command == frepo_command_init)
	{
		if (!settings_manifest_url_set(
			settings, repo))
		{
			fprintf(stderr,
				"Error: Failed to set repository URL.\n");
			return EXIT_FAILURE;
		}

		char brepo[strlen(repo) + 1];
		strcpy(brepo, repo);
		if (!settings_manifest_repo_set(
			settings, basename(brepo)))
		{
			fprintf(stderr,
				"Error: Failed to set repository name from URL.\n");
			return EXIT_FAILURE;
		}

		char mkdir_cmd[strlen(name) + 64];
		sprintf(mkdir_cmd, "mkdir -p %s > /dev/null", name);
		if (system(mkdir_cmd) != EXIT_SUCCESS)
		{
			fprintf(stderr, "Error: Failed to create '%s' directory.\n", name);
			return EXIT_FAILURE;
		}

		if (chdir(name) != 0)
		{
			fprintf(stderr, "Error: Failed to enter '%s' directory.\n", name);
			return EXIT_FAILURE;
		}

		/* TODO - Fix so that relative paths for repositories work with this. */

		if (mkdir(".frepo", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
		{
			if (errno != EEXIST)
			{
				fprintf(stderr, "Error: Failed to create '.frepo' directory.\n");
				return EXIT_FAILURE;
			}
		}

		if (!git_update(NULL, repo, NULL, NULL, branch, false))
		{
			fprintf(stderr, "Error: Failed to clone manifest repository.\n");
			return EXIT_FAILURE;
		}
	}

	const char* manifest_path = ".frepo/manifest.xml";

	const char* manifest_base = settings_manifest_path_get(settings);
	if (!manifest_base)
	{
		fprintf(stderr,
			"Error: Failed to get manifest path from settings.\n");
		return EXIT_FAILURE;
	}
	size_t manifest_base_size = strlen(manifest_base);

	char manifest_find_cmd[strlen(settings->manifest_repo) + 8];
	sprintf(manifest_find_cmd, "[ -d %s ]", settings->manifest_repo);

	if (system(manifest_find_cmd) != EXIT_SUCCESS)
	{
		do
		{
			char curdir[PATH_MAX];
			if ((getcwd(curdir, PATH_MAX) != curdir)
				|| (strcmp(curdir, "/") == 0)
				|| (chdir("..") != EXIT_SUCCESS)
				|| (system("[ -d .frepo ]") == EXIT_SUCCESS))
				break;
		} while (true);

		if ((system("[ -d .frepo ]") != EXIT_SUCCESS)
			&& (system(manifest_find_cmd) != EXIT_SUCCESS))
		{
			fprintf(stderr, "Error: Not in a frepo repository"
				", no manifest directory found.\n");
			return EXIT_FAILURE;
		}
	}

	if (system("[ -d .frepo ]") != EXIT_SUCCESS)
	{
		fprintf(stderr, "Warning: No .frepo directory found"
			", recreating but deletions may not be tracked.\n");

		if (mkdir(".frepo", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
		{
			if (errno != EEXIST)
			{
				fprintf(stderr, "Error: Failed to create '.frepo' directory.\n");
				return EXIT_FAILURE;
			}
		}

		char cmd[strlen(manifest_path) + manifest_base_size + 5];
		sprintf(cmd, "cp %s %s",
			manifest_base, manifest_path);
		if (system(cmd) != EXIT_SUCCESS)
		{
			/* Safely ignore output. */
		}
	}

	manifest_t* manifest
		= manifest_read(manifest_path);
	if (!manifest)
	{
		manifest = manifest_read(manifest_base);
		if (!manifest)
		{
			fprintf(stderr, "Error: Unable to read manifest file.\n");
			return EXIT_FAILURE;
		}
		if (command != frepo_command_init)
		{
			fprintf(stderr, "Warning: Failed to read stored manifest"
				", frepo may fail to track deletions cleanly.\n");
		}
	}

	manifest_t* manifest_filtered
		= manifest_group_filter(manifest,
			settings->group, settings->group_count);
	if (!manifest_filtered)
	{
		fprintf(stderr, "Error: Failed to filter manifest groups.\n");
		manifest_delete(manifest);
		return EXIT_FAILURE;
	}
	manifest_filtered->document = manifest->document;
	manifest->document = NULL;
	manifest_delete(manifest);
	manifest = manifest_filtered;

	if (threads <= 0)
		threads = manifest->threads;

	int ret = EXIT_FAILURE;
	switch (command)
	{
		case frepo_command_init:
			ret = frepo_init(
				manifest, settings->manifest_url,
				settings->mirror, threads);
			break;
		case frepo_command_sync:
			ret = frepo_sync(
				manifest,
				settings->manifest_repo,
				manifest_path,
				settings->manifest_url,
				force, branch,
				settings->group,
				settings->group_count,
				threads);
			break;
		case frepo_command_snapshot:
			ret = frepo_snapshot(
				manifest,
				settings->manifest_repo,
				manifest_base, name);
			break;
		case frepo_command_forall:
			ret = frepo_forall(manifest, fa_argc, fa_argv, print);
			break;
		default:
			ret = frepo_list(manifest);
			break;
	}

	if ((ret == EXIT_SUCCESS)
		&& ((command == frepo_command_init)
			|| (command == frepo_command_sync)))
	{
		char cmd[strlen(manifest_path) + manifest_base_size + 5];
		sprintf(cmd, "cp %s %s", manifest_base, manifest_path);
		if (system(cmd) != EXIT_SUCCESS)
			fprintf(stderr, "Warning: Failed to store current manifest state"
				", frepo may fail to track deletions cleanly.\n");

		if (!settings_write(
			settings, settings_path))
			fprintf(stderr, "Warning: Failed to write settings file.\n");
	}

	manifest_delete(manifest);
	settings_delete(settings);

	return ret;
}

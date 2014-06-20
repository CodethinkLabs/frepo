#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "git.h"
#include "xml.h"
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
	printf("%s init name -u manifest [-b branch] [-g groups] [--mirror]\n", prog);
	printf("%s sync [-f] [-b branch] [-g groups]\n", prog);
	printf("%s snapshot name [-g groups]\n", prog);
	printf("%s list [-g groups]\n", prog);
	printf("%s forall  [-g groups] [-p] -c command\n", prog);
}



static bool frepo_sync_manifest(manifest_t* manifest, bool mirror)
{
	if (!manifest)
		return false;

	unsigned i;
	for (i = 0; i < manifest->project_count; i++)
	{
		bool exists = git_exists(manifest->project[i].path);

		printf("%s repository (%u/%u) '%s'.\n",
			(exists ? "Updating" : "Cloning"),
			(i + 1), manifest->project_count,
			manifest->project[i].path);

		char* revision = NULL;
		bool revision_differs = false;

		if (exists && !mirror)
		{
			revision = git_current_branch(
				manifest->project[i].path);
			if (!revision)
			{
				fprintf(stderr, "Error: Failed to check current revision of '%s'.\n",
					manifest->project[i].path);
				continue;
			}

			revision_differs
				= (strcmp(revision, manifest->project[i].revision) != 0);
			if (revision_differs && !git_checkout(
				manifest->project[i].path,
				manifest->project[i].revision, false))
			{
				free(revision);
				fprintf(stderr, "Error: Failed to checkout revision '%s' of '%s'.\n",
					manifest->project[i].revision,
					manifest->project[i].path);
				continue;
			}
		}

		if (!git_update(
			manifest->project[i].path,
			manifest->project[i].remote,
			manifest->project[i].name,
			manifest->project[i].remote_name,
			manifest->project[i].revision, mirror))
		{
			fprintf(stderr, "Error: Failed to %s '%s'.\n",
				(exists ? "update" : "clone"),
				manifest->project[i].path);
		}

		unsigned j;
		for (j = 0; j < manifest->project[i].copyfile_count; j++)
		{
			char cmd[strlen(manifest->project[i].path)
				+ strlen(manifest->project[i].copyfile[j].source)
				+ strlen(manifest->project[i].copyfile[j].dest) + 16];
			sprintf(cmd, "cp %s/%s %s",
				manifest->project[i].path,
				manifest->project[i].copyfile[j].source,
				manifest->project[i].copyfile[j].dest);
			if (system(cmd) != EXIT_SUCCESS)
			{
				unsigned k;
				for (k = 0; k < i; k++)
					git_remove(manifest->project[k].path);
				fprintf(stderr,
					"Error: Failed to perform copy '%s' to '%s'"
					" for project '%s'\n",
					manifest->project[i].copyfile[j].source,
					manifest->project[i].copyfile[j].dest,
					manifest->project[i].path);
			}
		}

		if (revision_differs && !git_checkout(
			manifest->project[i].path,
			revision, false))
		{
			fprintf(stderr, "Error: Failed to revert '%s' to revision '%s'.\n",
				manifest->project[i].path, revision);
		}

		free(revision);
	}
	return true;
}

static int frepo_init(manifest_t* manifest, bool mirror)
{
	return (frepo_sync_manifest(manifest, mirror)
		? EXIT_SUCCESS : EXIT_FAILURE);
}

static int frepo_sync(
	manifest_t* manifest, const char* manifest_path,
	bool force, const char* branch,
	group_t* group, unsigned group_count)
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
	if (!git_uncomitted_changes("manifest", &manifest_uncommitted_changes))
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
		= git_current_commit("manifest");
	if (!manifest_head_old)
	{
		fprintf(stderr, "Error: Failed to get local manifest commit.\n");
		goto frepo_sync_failed;
	}

	printf("Updating manifest.\n");

	manifest_branch = git_current_branch("manifest");
	if (branch)
	{
		manifest_branch_old = manifest_branch;
		if (!git_checkout("manifest", branch, false))
		{
			fprintf(stderr, "Error: Failed to checkout manifest branch.\n");
			goto frepo_sync_failed;
		}
		manifest_branch = (char*)branch;
	}

	if (!git_update("manifest",
			NULL, NULL, NULL,
			manifest_branch, false))
	{
		fprintf(stderr, "Error: Failed to update manifest.\n");
		goto frepo_sync_failed;
	}

	manifest_head_latest
		= git_current_commit("manifest");
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

		if (!frepo_sync_manifest(manifest_updated, false))
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
		git_checkout("manifest", manifest_branch_old, false);
	if (manifest_head_old)
		git_reset_hard("manifest", manifest_head_old);
	manifest_delete(manifest_updated);
	manifest_delete(manifest_old);
	free(manifest_head_latest);
	free(manifest_head_old);
	free(manifest_branch_old);
	if (manifest_branch != branch)
		free(manifest_branch);
	return EXIT_FAILURE;
}

static int frepo_snapshot(manifest_t* manifest, const char* manifest_path, const char* name)
{
	bool changes;
	if (!git_uncomitted_changes("manifest", &changes))
	{
		fprintf(stderr, "Error: Failed to check for uncommitted changes to manifest.\n");
		return EXIT_FAILURE;
	}

	if (changes)
	{
		fprintf(stderr, "Error: Can't snapshot a manifest repository with uncommitted changes.\n");
		return EXIT_FAILURE;
	}

	char* branch = git_current_branch("manifest");
	if (!branch)
		branch = git_current_commit("manifest");

	if (!branch)
	{
		fprintf(stderr, "Error: Failed get current HEAD.\n");
		return EXIT_FAILURE;
	}

	if (!git_checkout("manifest", name, true))
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

	if (!git_commit("manifest", snapshot_message))
	{
		fprintf(stderr, "Error: Failed to commit snapshot, reverting.\n");
		if (!git_reset_hard("manifest", "HEAD"))
			fprintf(stderr, "Warning: Failed to reset uncommitted snapshot.\n");
		goto frepo_snapshot_failed;
	}

	if (!git_checkout("manifest", branch, false))
		fprintf(stderr, "Warning: Failed to revert manifest"
			" to previous branch '%s'.\n", branch);
	free(branch);

	return EXIT_SUCCESS;

frepo_snapshot_failed:
	if (!git_checkout("manifest", branch, false))
		fprintf(stderr, "Warning: Failed to revert manifest"
			" to previous branch '%s'.\n", branch);
	else if (!git_reset_hard("manifest", "HEAD"))
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

	const char* name   = NULL;
	const char* repo   = NULL;
	const char* branch = NULL;
	bool        force  = false;
	bool        print  = false;

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
		if (mkdir(name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
		{
			if (errno != EEXIST)
			{
				fprintf(stderr, "Error: Failed to create '%s' directory.\n", name);
				return EXIT_FAILURE;
			}
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

	if (system("[ -d manifest ]") != EXIT_SUCCESS)
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
			&& (system("[ -d manifest ]") != EXIT_SUCCESS))
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

		char cmd[strlen(manifest_path) + 64];
		sprintf(cmd, "cp manifest/default.xml %s", manifest_path);
		if (system(cmd) != EXIT_SUCCESS)
		{
			/* Safely ignore output. */
		}
	}

	manifest_t* manifest
		= manifest_read(manifest_path);
	if (!manifest)
	{
		manifest = manifest_read("manifest/default.xml");
		if (!manifest)
		{
			fprintf(stderr, "Error: Unable to read manifest file.\n");
			return EXIT_FAILURE;
		}
		fprintf(stderr, "Warning: Failed to read stored manifest"
			", frepo may fail to track deletions cleanly.\n");
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

	int ret = EXIT_FAILURE;
	switch (command)
	{
		case frepo_command_init:
			ret = frepo_init(manifest, settings->mirror);
			break;
		case frepo_command_sync:
			ret = frepo_sync(
				manifest, manifest_path,
				force, branch,
				settings->group,
				settings->group_count);
			break;
		case frepo_command_snapshot:
			ret = frepo_snapshot(manifest, manifest_path, name);
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
		char cmd[strlen(manifest_path) + 64];
		sprintf(cmd, "cp manifest/default.xml %s", manifest_path);
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

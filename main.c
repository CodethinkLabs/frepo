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


typedef enum
{
	frepo_command_init,
	frepo_command_sync,
	frepo_command_list,
	frepo_command_forall,
	frepo_command_count
} frepo_command_e;



void print_usage(const char* prog)
{
	printf("%s init name -u manifest [-b branch] [--mirror]\n", prog);
	printf("%s sync [-f]\n", prog);
	printf("%s list\n", prog);
	printf("%s forall -c command\n", prog);
}



static int frepo_init(manifest_t* manifest, bool mirror)
{
	unsigned i;
	for (i = 0; i < manifest->project_count; i++)
	{
		if (!git_clone(
			manifest->project[i].remote,
			manifest->project[i].name,
			manifest->project[i].path,
			manifest->project[i].remote_name,
			manifest->project[i].revision,
			mirror))
		{
			fprintf(stderr, "Error: Failed to clone '%s'\n",
				manifest->project[i].path);
			return EXIT_FAILURE;
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
				fprintf(stderr,
					"Error: Failed to perform copy '%s' to '%s'"
					" for project '%s'\n",
					manifest->project[i].copyfile[j].source,
					manifest->project[i].copyfile[j].dest,
					manifest->project[i].path);
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

static int frepo_sync(manifest_t* manifest, const char* manifest_path, bool force)
{
	char* manifest_head_old = NULL;
	char* manifest_head_latest = NULL;
	manifest_t* manifest_updated = NULL;
	manifest_t* manifest_old = NULL;
	manifest_t* manifest_new = NULL;
	manifest_t* manifest_unchanged = NULL;

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

	if (!git_fetch("manifest")
		|| !git_pull("manifest"))
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

		manifest_new = manifest_subtract(
			manifest_updated, manifest);

		manifest_unchanged = manifest_subtract(
			manifest_updated, manifest_new);
		if (!manifest_unchanged)
		{
			fprintf(stderr, "Error: Failed to read new manifest.\n");
			goto frepo_sync_failed;
		}
	}
	else
	{
		manifest_unchanged = manifest_copy(manifest);
	}

	if (manifest_unchanged)
	{
		unsigned i;
		for (i = 0; i < manifest_unchanged->project_count; i++)
		{
			printf("Checking for uncommitted changes in '%s' (%u/%u).\n",
				manifest_unchanged->project[i].path,
				(i + 1), manifest_unchanged->project_count);

			bool uncommitted_changes;
			if (!git_uncomitted_changes(
				manifest_unchanged->project[i].path, &uncommitted_changes))
			{
				fprintf(stderr, "Error: '%s' is deprecated but can't remove"
					" because checking for uncommitted changes failed.\n",
					manifest_unchanged->project[i].name);
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

	if (manifest_new)
	{
		unsigned i;
		for (i = 0; i < manifest_new->project_count; i++)
		{
			printf("Cloning new repository (%u/%u) '%s'.\n",
				(i + 1), manifest_new->project_count,
				manifest_new->project[i].path);

			if (!git_clone(
				manifest_new->project[i].remote,
				manifest_new->project[i].name,
				manifest_new->project[i].path,
				manifest_new->project[i].remote_name,
				manifest_new->project[i].revision,
				false))
			{
				unsigned k;
				for (k = 0; k < i; k++)
					git_remove(manifest_new->project[k].path);
				fprintf(stderr, "Error: Failed to pull new repository '%s'.\n",
					manifest_new->project[i].path);
				goto frepo_sync_failed;
			}

			unsigned j;
			for (j = 0; j < manifest_new->project[i].copyfile_count; j++)
			{
				char cmd[strlen(manifest_new->project[i].path)
					+ strlen(manifest_new->project[i].copyfile[j].source)
					+ strlen(manifest_new->project[i].copyfile[j].dest) + 16];
				sprintf(cmd, "cp %s/%s %s",
					manifest_new->project[i].path,
					manifest_new->project[i].copyfile[j].source,
					manifest_new->project[i].copyfile[j].dest);
				if (system(cmd) != EXIT_SUCCESS)
				{
					unsigned k;
					for (k = 0; k < i; k++)
						git_remove(manifest_new->project[k].path);
					fprintf(stderr,
						"Error: Failed to perform copy '%s' to '%s'"
						" for project '%s'\n",
						manifest_new->project[i].copyfile[j].source,
						manifest_new->project[i].copyfile[j].dest,
						manifest_new->project[i].path);
					goto frepo_sync_failed;
				}
			}
		}
	}

	if (manifest_unchanged)
	{
		unsigned i;
		for (i = 0; i < manifest_unchanged->project_count; i++)
		{
			printf("Updating existing repository (%u/%u) '%s'.\n",
				(i + 1), manifest_unchanged->project_count,
				manifest_unchanged->project[i].path);

			char* revision = git_current_branch(
				manifest_unchanged->project[i].path);
			if (!revision)
			{
				fprintf(stderr, "Error: Failed to check current revision of '%s'.\n",
					manifest_unchanged->project[i].path);
				continue;
			}

			bool revision_differs
				= (strcmp(revision, manifest_unchanged->project[i].revision) != 0);
			if (revision_differs && !git_checkout(
				manifest_unchanged->project[i].path,
				manifest_unchanged->project[i].revision))
			{
				free(revision);
				fprintf(stderr, "Error: Failed to checkout revision '%s' of '%s'.\n",
					manifest_unchanged->project[i].revision,
					manifest_unchanged->project[i].path);
				continue;
			}

			if (!git_fetch(manifest_unchanged->project[i].path)
				|| !git_pull(manifest_unchanged->project[i].path))
			{
				fprintf(stderr, "Error: Failed to update '%s'.\n",
					manifest_unchanged->project[i].path);
			}

			unsigned j;
			for (j = 0; j < manifest_unchanged->project[i].copyfile_count; j++)
			{
				char cmd[strlen(manifest_unchanged->project[i].path)
					+ strlen(manifest_unchanged->project[i].copyfile[j].source)
					+ strlen(manifest_unchanged->project[i].copyfile[j].dest) + 16];
				sprintf(cmd, "cp %s/%s %s",
					manifest_unchanged->project[i].path,
					manifest_unchanged->project[i].copyfile[j].source,
					manifest_unchanged->project[i].copyfile[j].dest);
				if (system(cmd) != EXIT_SUCCESS)
				{
					unsigned k;
					for (k = 0; k < i; k++)
						git_remove(manifest_unchanged->project[k].path);
					fprintf(stderr,
						"Error: Failed to perform copy '%s' to '%s'"
						" for project '%s'\n",
						manifest_unchanged->project[i].copyfile[j].source,
						manifest_unchanged->project[i].copyfile[j].dest,
						manifest_unchanged->project[i].path);
				}
			}

			if (revision_differs && !git_checkout(
				manifest_unchanged->project[i].path,
				manifest_unchanged->project[i].revision))
			{
				fprintf(stderr, "Error: Failed to revert '%s' to revision '%s'.\n",
					manifest_unchanged->project[i].path, revision);
			}

			free(revision);
		}
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
	manifest_delete(manifest_new);
	manifest_delete(manifest_unchanged);
	free(manifest_head_latest);
	free(manifest_head_old);
	return EXIT_SUCCESS;

frepo_sync_failed:
	if (manifest_head_old)
		git_reset_hard("manifest", manifest_head_old);
	manifest_delete(manifest_updated);
	manifest_delete(manifest_old);
	manifest_delete(manifest_new);
	manifest_delete(manifest_unchanged);
	free(manifest_head_latest);
	free(manifest_head_old);
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

static int frepo_forall(manifest_t* manifest, int argc, char** argv)
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
			fprintf(stderr, "Error: Command '%s' failed for project '%s'.\n",
				cmd, manifest->project[j].path);
			return EXIT_FAILURE;
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
	bool        mirror = false;
	bool        force  = false;

	int    fa_argc = 0;
	char** fa_argv = NULL;

	int a = 2;
	if (command == frepo_command_init)
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
					if (command != frepo_command_init)
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
				mirror = true;
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

		if (!git_clone(repo, NULL, NULL, NULL, branch, false))
		{
			fprintf(stderr, "Error: Failed to clone manifest repository.\n");
			return EXIT_FAILURE;
		}
	}

	const char* manifest_path = "manifest/default.xml";

	manifest_t* manifest
		= manifest_read(manifest_path);
	if (!manifest)
	{
		fprintf(stderr, "Error: Unable to read manifest file.\n");
		return EXIT_FAILURE;
	}

	int ret = EXIT_FAILURE;
	switch (command)
	{
		case frepo_command_init:
			ret = frepo_init(manifest, mirror);
			break;
		case frepo_command_sync:
			ret = frepo_sync(manifest, manifest_path, force);
			break;
		case frepo_command_forall:
			ret = frepo_forall(manifest, fa_argc, fa_argv);
			break;
		default:
			ret = frepo_list(manifest);
			break;
	}

	manifest_delete(manifest);
	return ret;
}

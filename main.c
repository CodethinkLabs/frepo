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
	printf("%s sync\n", prog);
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
	}
	return EXIT_SUCCESS;
}

static int frepo_sync(manifest_t* manifest, const char* manifest_path)
{
	fprintf(stderr, "Error: Sync function not yet implemented.\n");

	/* TODO - Find manifest. */
	/* TODO - Sync manifest. */

	/* TODO - Check for uncommitted changes/branches before deletion. */
	/* TODO - Delete removed repositories. */
	/* TODO - Delete unused directories. */

	/* TODO - Check for uncommitted changes. */

	/* TODO - Switch to manifest branch. */
	/* TODO - Update existing repositories. */
	/* TODO - Switch back to previous branch. */

	/* TODO - Pull new repositories. */
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
			ret = frepo_sync(manifest, manifest_path);
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

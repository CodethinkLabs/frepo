#include "git.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <assert.h>


bool git_clone(
	const char* repo, const char* path,
	const char* target, const char* remote_name,
	const char* branch, bool mirror)
{
	if (!repo)
		return false;

	char cmd[strlen(repo)
		+ (path ? strlen(path) + 1 : 0)
		+ (target ? strlen(target) + 1 : 0)
		+ (remote_name ? strlen(remote_name) + 10 : 0)
		+ (branch ? strlen(branch) + 4 : 0) + 64];

	if (branch)
	{
		sprintf(cmd, "git ls-remote --exit-code %s %s", repo, branch);
		if (system(cmd) != EXIT_SUCCESS)
			return false;
	}

	sprintf(cmd, "git clone %s", repo);

	if (path)
	{
		strcat(cmd, "/");
		strcat(cmd, path);
	}

	if (branch)
	{
		strcat(cmd, " -b ");
		strcat(cmd, branch);
	}

	if (target)
	{
		strcat(cmd, " ");
		strcat(cmd, target);
	}

	if (remote_name)
	{
		strcat(cmd, " --origin ");
		strcat(cmd, remote_name);
	}

	if (mirror)
		strcat(cmd, " --mirror");

	return (system(cmd) == EXIT_SUCCESS);
}



bool git__command(const char* path, const char* command)
{
	if (!path || !command)
		return false;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
		return false;
	if (chdir(path) != 0)
		return false;

	bool ret = (system(command) == 0);
	assert(chdir(pdir) == 0);
	return ret;
}

bool git_reset_hard(const char* path, const char* commit)
{
	if (!path || !commit)
		return false;

	char cmd[strlen(commit) + 64];
	sprintf(cmd, "git reset --hard %s", commit);
	return git__command(path, cmd);
}

bool git_fetch(const char* path)
{
	return git__command(path, "git fetch");
}

bool git_pull(const char* path)
{
	return git__command(path, "git pull");
}

bool git_remove(const char* path)
{
	if (!path) return false;
	char cmd[strlen(path) + 64];
	sprintf(cmd, "rm -rf %s", path);
	return (system(cmd) == 0);
}

bool git_checkout(const char* path, const char* revision)
{
	if (!path || !revision)
		return false;

	char cmd[strlen(revision) + 64];
	sprintf(cmd, "git checkout %s", revision);
	return git__command(path, cmd);
}



bool git_uncomitted_changes(const char* path, bool* changed)
{
	if (!path || !changed)
		return false;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
		return false;
	if (chdir(path) != 0)
		return false;

	*changed = (system("git diff --exit-code") != 0);
	assert(chdir(pdir) == 0);
	return true;
}



static char* git__pipe_read(const char* cmd)
{
	FILE* fp = popen(cmd, "r");
	if (!fp) return NULL;

	unsigned read = 0;
	unsigned size = 32;
	char*    result = NULL;

	do
	{
		size <<= 1;
		char* nresult
			= (char*)realloc(result, size);
		if (!nresult)
		{
			free(result);
			return NULL;
		}
		result = nresult;
		read += fread(&result[read], 1, (size - read), fp);
	} while (read >= size);

	if (read == 0)
		return NULL;

	if (result[read - 1] == '\n')
		read--;
	result[read] = '\0';

	char* nresult
		= (char*)realloc(
			result, (read + 1));
	if (nresult) result = nresult;

	pclose(fp);
	return result;
}

char* git_current_branch(const char* path)
{
	if (!path) return NULL;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
		return NULL;
	if (chdir(path) != 0)
		return NULL;

	char* branch
		= git__pipe_read("git rev-parse --symbolic-full-name --abbrev-ref HEAD");
	assert(chdir(pdir) == 0);
	return branch;
}

char* git_current_commit(const char* path)
{
	if (!path) return NULL;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
		return NULL;
	if (chdir(path) != 0)
		return NULL;

	char* commit
		= git__pipe_read("git rev-parse HEAD");
	assert(chdir(pdir) == 0);
	return commit;
}

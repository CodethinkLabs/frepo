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
	const char* path,
	const char* remote, const char* remote_path, const char* remote_name,
	const char* branch, bool mirror)
{
	if (!remote)
		return false;

	char cmd[strlen(remote)
		+ (remote_path ? strlen(remote_path) + 1 : 0)
		+ (remote_name ? strlen(remote_name) + 10 : 0)
		+ (path ? strlen(path) + 1 : 0)
		+ (branch ? strlen(branch) + 4 : 0) + 64];

	bool checkout_revision = false;
	if (branch)
	{
		sprintf(cmd, "git ls-remote --heads --exit-code %s", remote);

		if (remote_path)
		{
			strcat(cmd, "/");
			strcat(cmd, remote_path);
		}

		strcat(cmd, " ");
		strcat(cmd, branch);

		checkout_revision = (system(cmd) != EXIT_SUCCESS);
	}

	sprintf(cmd, "git clone %s", remote);

	if (remote_path)
	{
		strcat(cmd, "/");
		strcat(cmd, remote_path);
	}

	if (branch && !checkout_revision)
	{
		strcat(cmd, " -b ");
		strcat(cmd, branch);
	}

	char auto_path[strlen(remote)
		+ (remote_path ? strlen(remote_path) + 1 : 0) + 1];
	if (path)
	{
		strcat(cmd, " ");
		strcat(cmd, path);
	}
	else
	{
		strcpy(auto_path, remote);
		if (remote_path)
		{
			strcat(auto_path, "/");
			strcat(auto_path, remote_path);
		}

		unsigned pathlen = strlen(auto_path);
		while ((pathlen > 1)
			&& (auto_path[pathlen - 1] == '/'))
			auto_path[--pathlen] = '\0';

		char* npath = auto_path;
		unsigned i;
		for (i = 0; i < pathlen; i++)
		{
			if (auto_path[i] == '/')
				npath = &auto_path[i + 1];
		}
		pathlen -= i;

		if (!mirror
			&& (strcmp(&npath[pathlen - 4], ".git") == 0))
		{
			pathlen -= 4;
			npath[pathlen] = '\0';
		}
		path = npath;
	}

	if (remote_name)
	{
		strcat(cmd, " --origin ");
		strcat(cmd, remote_name);
	}

	if (mirror)
		strcat(cmd, " --mirror");

	if (system(cmd) != EXIT_SUCCESS)
		return false;

	if (checkout_revision)
		return git_checkout(path, branch, false);
	return true;
}



static bool git__command(const char* path, const char* command)
{
	if (!path || !command)
		return false;

	char pdir[PATH_MAX];
	if (getcwd(pdir, PATH_MAX) != pdir)
		return false;
	if (chdir(path) != 0)
		return false;

	bool ret = (system(command) == EXIT_SUCCESS);
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

bool git_fetch(const char* path, const char* remote)
{
	char cmd[(remote ? strlen(remote) : 0) + 64];
	strcpy(cmd, "git fetch");
	if (remote)
	{
		strcat(cmd, " ");
		strcat(cmd, remote);
	}
	return git__command(path, cmd);
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

bool git_checkout(const char* path, const char* revision, bool create)
{
	if (!path || !revision)
		return false;

	char cmd[strlen(revision) + 64];
	strcpy(cmd, "git checkout ");
	if (create)
		strcat(cmd, "-b ");
	strcat(cmd, revision);
	return git__command(path, cmd);
}

bool git_commit(const char* path, const char* message)
{
	if (!message)
		return false;

	char cmd[strlen(message) + 64];
	sprintf(cmd, "git commit -a -m \"%s\"", message);
	return git__command(path, cmd);
}

bool git_update(const char* path, const char* revision, const char* remote)
{
	if (!path || !revision)
		return false;

	if (!git_fetch(path, remote))
		return false;

	bool is_branch;
	if (!git_revision_is_branch(path, revision, &is_branch))
		return false;

	return (is_branch
		? git_pull(path)
		: git_checkout(path, revision, false));
}



bool git_revision_is_branch(const char* path, const char* revision, bool* is_branch)
{
	if (!path || !revision || !is_branch)
		return false;

	char cmd[strlen(revision) + 64];
	sprintf(cmd, "git ls-remote --heads --exit-code . %s", revision);
	*is_branch = git__command(path, cmd);
	return true;
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

	bool unstaged
		= (system("git diff --quiet --exit-code") != EXIT_SUCCESS);
	bool uncommitted
		= (system("git diff --cached --quiet --exit-code") != EXIT_SUCCESS);

	*changed = (unstaged || uncommitted);

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
	if (branch && (strcmp(branch, "HEAD") == 0))
	{
		free(branch);
		branch = git__pipe_read("git rev-parse HEAD");
	}

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

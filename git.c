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

	return (system(cmd) == 0);
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



char* git_current_branch(const char* path)
{
	/* TODO - Implement, popen, bash, grumble. */
	return NULL;
}

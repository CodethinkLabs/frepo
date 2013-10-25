#ifndef __git_h__
#define __git_h__

#include <stdbool.h>

extern bool git_clone(
	const char* repo, const char* path,
	const char* target, const char* remote_name,
	const char* branch, bool mirror);

extern bool  git_uncomitted_changes(const char* path, bool* changed);
extern char* git_current_branch(const char* path);

#endif

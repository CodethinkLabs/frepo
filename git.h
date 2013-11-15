#ifndef __git_h__
#define __git_h__

#include <stdbool.h>

extern bool git_clone(
	const char* path,
	const char* remote, const char* remote_path, const char* remote_name,
	const char* branch, bool mirror);

extern bool git_reset_hard(const char* path, const char* commit);
extern bool git_fetch(const char* path, const char* remote);
extern bool git_pull(const char* path);
extern bool git_remove(const char* path);
extern bool git_checkout(const char* path, const char* revision, bool create);
extern bool git_commit(const char* path, const char* message);
extern bool git_update(const char* path, const char* revision, const char* remote);

extern bool  git_revision_is_branch(const char* path, const char* revision, bool* is_branch);
extern bool  git_uncomitted_changes(const char* path, bool* changed);
extern char* git_current_branch(const char* path);
extern char* git_current_commit(const char* path);

#endif

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

#ifndef __git_h__
#define __git_h__

#include <stdbool.h>

extern bool git_reset_hard(const char* path, const char* commit);
extern bool git_fetch(const char* path, const char* remote);
extern bool git_pull(const char* path);
extern bool git_remove(const char* path);
extern bool git_exists(const char* path);
extern bool git_checkout(const char* path, const char* revision, bool create);
extern bool git_commit(const char* path, const char* message);

extern bool git_update(
	const char* path,
	const char* remote, const char* remote_path, const char* remote_name,
	const char* revision, bool mirror);

extern bool  git_revision_is_branch(const char* path, const char* revision, bool* is_branch);
extern bool  git_uncomitted_changes(const char* path, bool* changed);
extern char* git_current_branch(const char* path);
extern char* git_current_commit(const char* path);

#endif

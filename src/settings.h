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

#ifndef __settings_h__
#define __settings_h__

#include "group.h"

typedef struct
{
	char*    manifest_repo;
	char*    manifest_name;
	char*    manifest_path; /* Autogenerated. */
	char*    manifest_url;
	bool     mirror;
	group_t* group;
	unsigned group_count;
	char*    group_string; /* Do not modify. */
} settings_t;

extern settings_t* settings_create(bool mirror);
extern void        settings_delete(settings_t* settings);

extern bool settings_manifest_repo_set(
	settings_t* settings, const char* repo);
extern bool settings_manifest_name_set(
	settings_t* settings, const char* name);
extern bool settings_manifest_url_set(
	settings_t* settings, const char* url);

extern const char* settings_manifest_path_get(
	settings_t* settings);
extern const char* settings_manifest_url_get(
	settings_t* settings);

extern settings_t* settings_read(const char* path);
extern bool        settings_write(settings_t* settings, const char* path);

#endif

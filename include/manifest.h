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

#ifndef __manifest_h__
#define __manifest_h__

#include "xml.h"
#include "group.h"
#include <stdbool.h>

typedef struct
{
	const char* name;
	const char* fetch;
} remote_t;

typedef struct
{
	const char* source;
	const char* dest;
} copyfile_t;

typedef struct
{
	const char* path;
	const char* name;
	const char* remote;
	const char* remote_name;
	const char* revision;
	copyfile_t* copyfile;
	unsigned    copyfile_count;
	group_t*    group;
	unsigned    group_count;
} project_t;

typedef struct
{
	remote_t*  remote;
	unsigned   remote_count;
	project_t* project;
	unsigned   project_count;
	xml_tag_t* document;
	long int   threads;
} manifest_t;



extern void        manifest_delete(manifest_t* manifest);
extern manifest_t* manifest_parse(xml_tag_t* document);
extern manifest_t* manifest_read(const char* path);

extern manifest_t* manifest_copy(manifest_t* a);
extern manifest_t* manifest_subtract(manifest_t* a, manifest_t* b);

extern bool manifest_write_snapshot(manifest_t* manifest, const char* path);

extern manifest_t* manifest_group_filter(
	manifest_t* manifest,
	group_t* filter, unsigned filter_count);

#endif

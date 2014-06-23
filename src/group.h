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

#ifndef __group_h__
#define __group_h__

#include <stdbool.h>

typedef struct
{
	const char* name;
	unsigned    size;
	bool        exclude;
} group_t;

extern bool group_list_match(
	const char* name, unsigned size,
	group_t* list, unsigned list_count,
	unsigned* index);
extern bool group_list_add(
	const char* name, unsigned size, bool exclude,
	group_t** list, unsigned* list_count);
extern bool group_list_remove(
	const char* name, unsigned size,
	group_t** list, unsigned* list_count);

extern bool group_list_copy(
	group_t* list, unsigned list_count,
	group_t** copy, unsigned* copy_count);

extern bool group_list_parse(
	const char* groups, bool filter,
	group_t** list, unsigned* list_count);

#endif

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

#include "group.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/utsname.h>


bool group_list_match(
	const char* name, unsigned size,
	group_t* list, unsigned list_count,
	unsigned* index)
{
	if (!name || (size == 0) || !list)
		return false;

	unsigned i;
	for (i = 0; i < list_count; i++)
	{
		if (list[i].size != size)
			continue;
		if (strncmp(list[i].name, name, size) == 0)
		{
			if (index)
				*index = i;
			return true;
		}
	}

	return false;
}

bool group_list_add(
	const char* name, unsigned size, bool exclude,
	group_t** list, unsigned* list_count)
{
	if (!name || (size == 0)
		|| !list || !list_count)
		return false;

	group_list_remove(name, size, list, list_count);

	group_t* nlist = (group_t*)realloc(*list,
		(sizeof(group_t) * (*list_count + 1)));
	if (!nlist) return false;
	(*list) = nlist;

	unsigned c = (*list_count);
	(*list)[c].name    = name;
	(*list)[c].size    = size;
	(*list)[c].exclude = exclude;
	(*list_count)++;

	return true;
}

bool group_list_remove(
	const char* name, unsigned size,
	group_t** list, unsigned* list_count)
{
	if (!name || (size == 0)
		|| !list || !list_count)
		return false;

	unsigned i;
	while (group_list_match(
		name, size, *list, *list_count, &i))
	{
		unsigned j;
		for (j = i; j < (*list_count - 1); j++)
			(*list)[j] = (*list)[j + 1];
		(*list_count)--;
	}

	if (*list_count == 0)
	{
		free(*list);
		*list = NULL;
		return true;
	}

	group_t* nlist = realloc(*list,
		sizeof(group_t) * (*list_count));
	if (nlist) *list = nlist;

	return true;
}

bool group_list_copy(
	group_t* list, unsigned list_count,
	group_t** copy, unsigned* copy_count)
{
	if (!copy || !copy_count)
		return false;

	if (!list)
	{
		*copy = NULL;
		*copy_count = 0;
		return true;
	}

	group_t* ncopy = (group_t*)malloc(
		sizeof(group_t) * list_count);
	if (!ncopy) return false;
	memcpy(ncopy, list,
		(sizeof(group_t) * list_count));

	*copy = ncopy;
	*copy_count = list_count;
	return true;
}

bool group_list_parse(
	const char* groups, bool filter,
	group_t** list, unsigned* list_count)
{
	if (!groups)
		return false;

	group_t* nlist = NULL;
	unsigned nlist_count = 0;

	if (list && list_count
		&& !group_list_copy(
			*list, *list_count,
			&nlist, &nlist_count))
		return false;

	unsigned i = 0;
	while (groups[i] != '\0')
	{
		if (groups[i] == ',')
		{
			i++;
			continue;
		}

		bool exclusion = false;
		if (filter)
		{
			if (groups[i] == '+')
			{
				i++;
			}
			else if (groups[i] == '-')
			{
				exclusion = true;
				i++;
			}
		}

		const char* next = strchr(&groups[i], ',');
		unsigned size = (next
			? ((uintptr_t)next - (uintptr_t)&groups[i])
			: strlen(&groups[i]));

		if (!group_list_add(
			&groups[i], size, exclusion,
			&nlist, &nlist_count))
		{
			free(nlist);
			return false;
		}

		i += size + (next ? 1 : 0);
	}

	if (nlist && (!list || !list_count))
	{
		free(nlist);
		return false;
	}

	free(*list);
	*list = nlist;
	*list_count = nlist_count;
	return true;
}


// Sorted in order of least common to most since groups can be processed
// in reverse order and use the last match, so it's more likely to match faster
// if the most common is last.
static const group_t platform_groups[] =
{
	{
		"platform-darwin",
		15,
		false,
	},
	{
		"platform-windows",
		16,
		false,
	},
	{
		"platform-linux",
		14,
		false,
	}
};
#define PLATFORM_GROUPS_COUNT (sizeof(platform_groups)/sizeof(platform_groups[0]))


const group_t* group__list_find_by_name(
	const char* platform,
	unsigned* platform_groups_count)
{
	const group_t* match;
	for (match = &platform_groups[PLATFORM_GROUPS_COUNT - 1];
		match >= platform_groups;
		match--)
	{
		if (strncasecmp(
			platform,
			&match->name[strlen("platform-")],
			match->size - strlen("platform-")) == 0)
		{
			*platform_groups_count = 1;
			return match;
		}
	}
	return NULL;
}


const group_t* group_list_parse_platform(
	const char* platform,
	unsigned* platform_groups_count)
{
	if (strcmp(platform, "all") == 0)
	{
		*platform_groups_count = PLATFORM_GROUPS_COUNT;
		return platform_groups;
	}

	if (strcmp(platform, "auto") == 0)
	{
		struct utsname u;
		if (uname(&u) != 0)
			// This should never happen in practise
			// because &u is always a valid pointer,
			return NULL;
		return group__list_find_by_name(
			u.sysname,
			platform_groups_count);
	}

	return group__list_find_by_name(platform, platform_groups_count);
}

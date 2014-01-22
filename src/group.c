#include "group.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>



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

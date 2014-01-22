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

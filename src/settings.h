#ifndef __settings_h__
#define __settings_h__

#include "group.h"

typedef struct
{
	bool     mirror;
	group_t* group;
	unsigned group_count;
	char*    group_string; /* Do not modify. */
} settings_t;

extern settings_t* settings_create(bool mirror);
extern void        settings_delete(settings_t* settings);

extern settings_t* settings_read(const char* path);
extern bool        settings_write(settings_t* settings, const char* path);

#endif

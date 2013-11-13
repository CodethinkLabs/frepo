#ifndef __manifest_h__
#define __manifest_h__

#include "xml.h"

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
} project_t;

typedef struct
{
	remote_t*  remote;
	unsigned   remote_count;
	project_t* project;
	unsigned   project_count;
	xml_tag_t* document;
} manifest_t;



extern void        manifest_delete(manifest_t* manifest);
extern manifest_t* manifest_parse(xml_tag_t* document);
extern manifest_t* manifest_read(const char* path);

extern manifest_t* manifest_copy(manifest_t* a);
extern manifest_t* manifest_subtract(manifest_t* a, manifest_t* b);

#endif

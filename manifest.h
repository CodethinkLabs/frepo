#ifndef __manifest_h__
#define __manifest_h__

#include "xml.h"

typedef struct
{
	const char* path;
	const char* name;
	const char* remote;
	const char* remote_name;
	const char* revision;
} project_t;

typedef struct
{
	project_t* project;
	unsigned   project_count;
	xml_tag_t* document;
} manifest_t;



extern void        manifest_delete(manifest_t* manifest);
extern manifest_t* manifest_parse(xml_tag_t* document);
extern manifest_t* manifest_read(const char* path);

#endif

#ifndef __xml_h__
#define __xml_h__

typedef struct xml_tag_s   xml_tag_t;
typedef struct xml_field_s xml_field_t;

struct xml_field_s
{
	char* name;
	char* data;
};

struct xml_tag_s
{
	char*         name;
	xml_tag_t*    parent;
	xml_field_t** field;
	unsigned      field_count;
	xml_tag_t**   tag;
	unsigned      tag_count;
};



extern xml_tag_t*  xml_document_parse(const char* source);
extern const char* xml_tag_field(xml_tag_t* tag, const char* name);
extern void        xml_tag_delete(xml_tag_t* tag);

#endif

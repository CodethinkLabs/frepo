#include "xml.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>



xml_tag_t* xml__tag_create(const char* name, xml_tag_t* parent);
bool       xml__tag_append_field(xml_tag_t* tag, xml_field_t* field);
bool       xml__tag_insert_tag(xml_tag_t* tag, xml_tag_t* child);



unsigned xml__parse_comment(const char* source)
{
	if (!source)
		return 0;
	if (strncmp(source, "<!--", 4) != 0)
		return 0;

	unsigned i;
	for (i = 4; source[i] != '\0'; i++)
	{
		if (strncmp(&source[i], "-->", 3) == 0)
		{
			i += 3;
			break;
		}
	}

	return i;
}

unsigned xml__parse_whitespace(const char* source)
{
	if (!source)
		return 0;

	unsigned i;
	for (i = 0; isspace(source[i]); i++);

	unsigned c = xml__parse_comment(&source[i]);
	if (c)
	{
		i += c;
		i += xml__parse_whitespace(&source[i]);
	}

	return i;
}

unsigned xml__parse_field(const char* source, xml_field_t** field)
{
	if (!source)
		return 0;

	unsigned i = 0;
	i += xml__parse_whitespace(&source[i]);

	const char* name = &source[i];
	if (!isalpha(source[i])
		&& (source[i] != '_'))
		return 0;
	unsigned n;
	for (n = 1; isalnum(source[i + n])
		|| (source[i + n] == '_')
		|| (source[i + n] == '-')
		; n++);
	i += n;

	if (source[i++] != '=')
		return 0;

	if (source[i++] != '\"')
		return 0;
	const char* data = &source[i];
	unsigned d;
	for (d = 0; (source[i + d] != '\"')
		&& (source[i + d] != '\0'); d++);
	i += d;
	if (source[i++] != '\"')
		return 0;

	i += xml__parse_whitespace(&source[i]);

	if (field)
	{
		xml_field_t* nfield
			= (xml_field_t*)malloc(sizeof(xml_field_t)
				+ n + d + 2);
		if (!nfield) return 0;

		nfield->name = (char*)((uintptr_t)nfield + sizeof(xml_field_t));
		nfield->data = (char*)((uintptr_t)nfield->name + n + 1);

		memcpy(nfield->name, name, n);
		nfield->name[n] = '\0';

		memcpy(nfield->data, data, d);
		nfield->data[d] = '\0';

		*field = nfield;
	}

	return i;
}



xml_tag_t* xml__tag_create(const char* name, xml_tag_t* parent)
{
	xml_tag_t* tag
		= (xml_tag_t*)malloc(sizeof(xml_tag_t)
			+ (name ? strlen(name) + 1 : 0));
	if (!tag) return NULL;

	tag->name = NULL;
	if (name)
	{
		tag->name = (char*)((uintptr_t)tag + sizeof(xml_tag_t));
		strcpy(tag->name, name);
	}

	tag->parent = parent;
	tag->field = NULL;
	tag->field_count = 0;
	tag->tag = NULL;
	tag->tag_count = 0;

	return tag;
}

void xml_tag_delete(xml_tag_t* tag)
{
	if (!tag)
		return;

	if (tag->field)
	{
		unsigned i;
		for (i = 0; i < tag->field_count; i++)
			free(tag->field[i]);
		free(tag->field);
	}

	if (tag->tag)
	{
		unsigned i;
		for (i = 0; i < tag->tag_count; i++)
			xml_tag_delete(tag->tag[i]);
		free(tag->tag);
	}

	free(tag);
}

bool xml__tag_append_field(xml_tag_t* tag, xml_field_t* field)
{
	if (!tag || !field)
		return false;

	xml_field_t** nfield
		= (xml_field_t**)realloc(tag->field,
			(tag->field_count + 1) * sizeof(xml_field_t*));
	if (!nfield) return false;

	tag->field = nfield;
	tag->field[tag->field_count++] = field;
	return true;
}

bool xml__tag_insert_tag(xml_tag_t* tag, xml_tag_t* child)
{
	if (!tag || !child)
		return false;

	xml_tag_t** ntag
		= (xml_tag_t**)realloc(tag->tag,
			(tag->tag_count + 1) * sizeof(xml_tag_t*));
	if (!ntag) return false;

	tag->tag = ntag;
	tag->tag[tag->tag_count++] = child;
	child->parent = tag;
	return true;
}



unsigned xml__parse_tag(const char* source, xml_tag_t** tag)
{
	if (!source)
		return 0;

	unsigned i = 0;
	i += xml__parse_whitespace(&source[i]);

	if (source[i++] != '<')
		return 0;

	const char* name = &source[i];
	if (!isalpha(source[i])
		&& (source[i] != '_'))
		return 0;
	unsigned n;
	for (n = 1; isalnum(source[i + n]) || (source[i + n] == '_'); n++);
	i += n;

	i += xml__parse_whitespace(&source[i]);

	char nstr[n + 1];
	memcpy(nstr, name, n);
	nstr[n] = '\0';

	xml_tag_t* ntag = xml__tag_create(nstr, NULL);
	if (!ntag) return 0;

	xml_field_t* field;
	unsigned     field_length;
	while (field_length = xml__parse_field(&source[i], &field))
	{
		if (!xml__tag_append_field(ntag, field))
		{
			free(field);
			xml_tag_delete(ntag);
			return 0;
		}

		i += field_length;
	}

	i += xml__parse_whitespace(&source[i]);

	bool empty = (source[i] == '/');
	if (empty) i++;

	if (source[i++] != '>')
	{
		xml_tag_delete(ntag);
		return 0;
	}
	i += xml__parse_whitespace(&source[i]);

	if (!empty)
	{
		xml_tag_t* ctag;
		unsigned   ctag_length;
		while (ctag_length = xml__parse_tag(&source[i], &ctag))
		{
			if (!xml__tag_insert_tag(ntag, ctag))
			{
				xml_tag_delete(ctag);
				xml_tag_delete(ntag);
				return 0;
			}

			i += ctag_length;
		}

		i += xml__parse_whitespace(&source[i]);
		if ((strncmp(&source[i], "</", 2) != 0)
			|| (strncmp(&source[i + 2], name, n) != 0)
			|| (source[i + 2 + n] != '>'))
		{
			xml_tag_delete(ntag);
			return 0;
		}
		i += (2 + n + 1);
		i += xml__parse_whitespace(&source[i]);
	}

	if (tag)
		*tag = ntag;
	else
		xml_tag_delete(ntag);

	return i;
}



xml_tag_t* xml_document_parse(const char* source)
{
	xml_tag_t* document
		= xml__tag_create(NULL, NULL);
	if (!document) return NULL;

	unsigned i = 0;
	i += xml__parse_whitespace(&source[i]);

	if (strncmp(&source[i], "<?xml", 5) == 0)
	{
		i += 5;

		xml_field_t* field;
		unsigned     field_length;
		while (field_length = xml__parse_field(&source[i], &field))
		{
			if (!xml__tag_append_field(document, field))
			{
				free(field);
				xml_tag_delete(document);
				return NULL;
			}

			i += field_length;
		}

		i += xml__parse_whitespace(&source[i]);
		if (strncmp(&source[i], "?>", 2) != 0)
		{
			xml_tag_delete(document);
			return NULL;
		}
		i += 2;
		i += xml__parse_whitespace(&source[i]);
	}

	xml_tag_t* tag;
	unsigned   tag_length;
	while (tag_length = xml__parse_tag(&source[i], &tag))
	{
		if (!xml__tag_insert_tag(document, tag))
		{
			xml_tag_delete(tag);
			xml_tag_delete(document);
			return NULL;
		}

		i += tag_length;
	}
	i += xml__parse_whitespace(&source[i]);

	if (source[i] != '\0')
	{
		xml_tag_delete(document);
		return NULL;
	}

	return document;
}



const char* xml_tag_field(xml_tag_t* tag, const char* name)
{
	if (!tag || !name)
		return NULL;

	unsigned i;
	for (i = 0; i < tag->field_count; i++)
	{
		if (strcmp(tag->field[i]->name, name) == 0)
			return tag->field[i]->data;
	}

	return NULL;
}

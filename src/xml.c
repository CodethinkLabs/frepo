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

#include "xml.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>

#include <expat.h>


xml_tag_t* xml__tag_create(const char* name, xml_tag_t* parent);
bool       xml__tag_append_field(xml_tag_t* tag, xml_field_t* field);
bool       xml__tag_insert_tag(xml_tag_t* tag, xml_tag_t* child);



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

xml_field_t* xml__field_create(const char* name, const char* data)
{
	size_t n = strlen(name);
	size_t d = strlen(data);
	xml_field_t* nfield
	        = (xml_field_t*)malloc(sizeof(xml_field_t)
	                + n + d + 2);
	if (!nfield) return NULL;

	nfield->name = (char*)((uintptr_t)nfield + sizeof(xml_field_t));
	nfield->data = (char*)((uintptr_t)nfield->name + n + 1);

	memcpy(nfield->name, name, n);
	nfield->name[n] = '\0';

	memcpy(nfield->data, data, d);
	nfield->data[d] = '\0';

	return nfield;
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

typedef enum
{
	xml__RESULT_SUCCESS,
	xml__RESULT_CREATE_TAG_FAILED,
	xml__RESULT_INSERT_TAG_FAILED,
	xml__RESULT_CREATE_FIELD_FAILED,
	xml__RESULT_APPEND_FIELD_FAILED,
} xml__result_e;

const char* xml__result_string(xml__result_e e)
{
	switch (e) {
		case xml__RESULT_SUCCESS:
			return "Succeded";
		case xml__RESULT_CREATE_TAG_FAILED:
			return "Creating tag value failed";
		case xml__RESULT_INSERT_TAG_FAILED:
			return "Inserting tag into parent failed";
		case xml__RESULT_CREATE_FIELD_FAILED:
			return "Creating field value failed";
		case xml__RESULT_APPEND_FIELD_FAILED:
			return "Appending field into tag failed";
		default:
			return "INVAILD RESULT VALUE!";
	}
}

typedef struct xml__state_s
{
	XML_Parser parser;
	xml_tag_t* current;
	xml__result_e result;
} xml__state_t;

void xml__stop_parser(xml__state_t* state, xml__result_e code)
{
	enum XML_Status res = XML_StopParser(state->parser, XML_FALSE);
	// We should only stop once,
	// so shouldn't fail from already having stopped.
	assert(res == XML_STATUS_OK);
	(void)res;
	state->result = code;
}

static void XMLCALL xml__start_element(void* ud, const XML_Char* name, const XML_Char** attrs)
{
#ifdef XML_UNICODE
#error "Conversion from wide characters to multibyte streams is not implemented"
#else
	xml__state_t* state = (xml__state_t*)ud;
	xml_tag_t* ntag = xml__tag_create(name, state->current);
	if (!ntag)
	{
		xml__stop_parser(state, xml__RESULT_CREATE_TAG_FAILED);
		return;
	}

	while (attrs[0] != NULL)
	{
		xml_field_t *field = xml__field_create(attrs[0], attrs[1]);
		if (!field)
		{
			xml_tag_delete(ntag);
			xml__stop_parser(state, xml__RESULT_CREATE_FIELD_FAILED);
			return;
		}

		if (!xml__tag_append_field(ntag, field))
		{
			free(field);
			xml_tag_delete(ntag);
			xml__stop_parser(state, xml__RESULT_APPEND_FIELD_FAILED);
			return;
		}
		attrs += 2;
	}

	state->current = ntag;
#endif
}

static void XMLCALL xml__end_element(void* ud, const XML_Char* /*name*/)
{
	xml__state_t* state = (xml__state_t*)ud;
	// If we stop in the element start handler the end element handler may
	// still be called when stopping in an empty element, so we ignore this
	// event if we are flagged as in an error state.
	if (state->result != xml__RESULT_SUCCESS)
	{
		// We assume if an error happened in an empty element start
		// that the current element was never updated so we don't
		// need to reset it.
		return;
	}

	// finish the current tag by inserting it into the parent tag
	xml_tag_t *current = state->current;
	xml_tag_t *parent = current->parent;
	if (!xml__tag_insert_tag(parent, current))
	{
		xml_tag_delete(current);
		xml__stop_parser(state, xml__RESULT_INSERT_TAG_FAILED);
		// Intentional fallthrough
	}
	state->current = parent;
}

xml_tag_t* xml_document_parse(const char* source, size_t len)
{
	if (len > INT_MAX) {
		fprintf(stderr, "Error: Document size %zu exceeds maximum expat's API can accept.\n", len);
		return NULL;
	}

	xml__state_t state;
	state.result = xml__RESULT_SUCCESS;
	xml_tag_t* document = xml__tag_create(NULL, NULL);
	if (!document) {
		fprintf(stderr, "Error: Failed to create empty document tag.\n");
		return NULL;
	}
	state.current = document;

	state.parser = XML_ParserCreate(NULL);
	if (!state.parser) {
		fprintf(stderr, "Error: Failed to create XML parser.\n");
		return NULL;
	}
	XML_SetElementHandler(state.parser, xml__start_element, xml__end_element);
	XML_SetUserData(state.parser, &state);

	enum XML_Status result = XML_Parse(state.parser, source, len, /*isFinal*/ 1);
	if (result != XML_STATUS_OK) {
		fprintf(stderr,
			"Error: Parsing failed with parser status: %s and processor status: %s.\n",
			XML_ErrorString(XML_GetErrorCode(state.parser)),
			xml__result_string(state.result));
		// Free from the root document instead of the current state
		// since delete is recursive and will get the current tag
		// but the current tag may not be popped off the stack back
		// to the root document tag in the case of error.
		xml_tag_delete(document);
		XML_ParserFree(state.parser);
		return NULL;
	}

	XML_ParserFree(state.parser);
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

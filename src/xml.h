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

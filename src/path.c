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

#include "path.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <libgen.h>



char* path_join(const char* base, const char* path)
{
	if (!path)
		return NULL;

	if (!base || (path[0] == '/'))
		return strdup(path);

	if (strchr(path, ':') != NULL)
		return strdup(path);

	const char* uri = base;
	int uri_len = 0;

	const char* uri_end = strstr(base, "://");
	if (uri_end != NULL)
	{
		uri_len = (int)((uintptr_t)uri_end - (uintptr_t)uri) + 3;
		base = &base[uri_len];
	}

	char nbase[strlen(base) + 1];
	strcpy(nbase, base);
	char* pbase = nbase;

	while(path[0] == '.')
	{
		if (path[1] == '/')
		{
			path = &path[2];
			continue;
		}

		if (path[1] == '\0')
		{
			path = &path[1];
			break;
		}

		if (path[1] == '.')
		{
			if (path[2] == '\0')
			{
				pbase = dirname(pbase);
				path = &path[2];
				break;
			}

			if (path[2] == '/')
			{
				pbase = dirname(pbase);
				path = &path[3];
				continue;
			}
		}

		break;
	}

	bool needs_slash
		= (pbase[strlen(pbase) - 1] != '/');

	char* rpath = (char*)malloc(
		uri_len + strlen(pbase) + 1 + strlen(path) + 1);
	if (!rpath) return NULL;

	if (strcmp(pbase, ".") == 0)
	{
		pbase = "";
		needs_slash = false;
	}

	sprintf(rpath, "%.*s%s%s%s",
		uri_len, uri, pbase,
		(needs_slash ? "/" : ""), path);
	return rpath;
}

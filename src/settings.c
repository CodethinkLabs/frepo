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

#include "settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


const char* manifest_repo_default = "manifest";
const char* manifest_name_default = "default.xml";


settings_t* settings_create(bool mirror)
{
	settings_t* settings
		= (settings_t*)malloc(
			sizeof(settings_t));
	if (!settings) return NULL;

	settings->manifest_repo = (char*)manifest_repo_default;
	settings->manifest_name = (char*)manifest_name_default;
	settings->manifest_path = NULL;
	settings->mirror        = mirror;
	settings->group         = NULL;
	settings->group_count   = 0;
	settings->group_string  = NULL;
	return settings;
}

void settings_delete(settings_t* settings)
{
	if (!settings)
		return;
	if (settings->manifest_repo != manifest_repo_default)
		free(settings->manifest_repo);
	if (settings->manifest_name != manifest_name_default)
		free(settings->manifest_name);
	free(settings->manifest_path);
	free(settings->group);
	free(settings->group_string);
	free(settings);
}



bool settings_manifest_repo_set(
	settings_t* settings, const char* repo)
{
	if (!settings)
		return false;

	if (!repo)
	{
		if (settings->manifest_repo != manifest_repo_default)
			free(settings->manifest_repo);
		settings->manifest_repo = (char*)manifest_repo_default;
	}
	else
	{
		size_t rlen = strlen(repo) + 1;
		char* nrepo = (char*)malloc(rlen);
		if (!nrepo) return false;
		memcpy(nrepo, repo, rlen);
		settings->manifest_repo = nrepo;
	}

	free(settings->manifest_path);
	settings->manifest_path = NULL;
	return true;
}

bool settings_manifest_name_set(
	settings_t* settings, const char* name)
{
	if (!settings)
		return false;

	if (!name)
	{
		if (settings->manifest_name != manifest_name_default)
			free(settings->manifest_name);
		settings->manifest_name = (char*)manifest_name_default;
	}
	else
	{
		size_t rlen = strlen(name) + 1;
		char* nname = (char*)malloc(rlen);
		if (!nname) return false;
		memcpy(nname, name, rlen);
		settings->manifest_name = nname;
	}

	free(settings->manifest_path);
	settings->manifest_path = NULL;
	return true;
}

bool settings_manifest_url_set(
	settings_t* settings, const char* url)
{
	if (!settings)
		return false;

	if (!url)
	{
		if (settings->manifest_url)
			free(settings->manifest_url);
		settings->manifest_url = NULL;
	}
	else
	{
		size_t rlen = strlen(url) + 1;
		char* nurl = (char*)malloc(rlen);
		if (!nurl) return false;
		memcpy(nurl, url, rlen);
		settings->manifest_url = nurl;
	}

	return true;
}



const char* settings_manifest_path_get(settings_t* settings)
{
	if (!settings)
		return NULL;

	if (!settings->manifest_path)
	{
		size_t rlen = strlen(settings->manifest_repo);
		size_t nlen = strlen(settings->manifest_name);
		size_t plen = rlen + 1 + nlen + 1;
		char* npath = (char*)malloc(plen);
		if (!npath) return NULL;
		sprintf(npath, "%s/%s",
			settings->manifest_repo,
			settings->manifest_name);
		settings->manifest_path = npath;
	}

	return (const char*)settings->manifest_path;
}

const char* settings_manifest_url_get(
	settings_t* settings)
{
	return (const char*)settings->manifest_url;
}



settings_t* settings_read(const char* path)
{
	settings_t* settings
		= settings_create(false);
	if (!settings) return NULL;

	FILE* fp = fopen(path, "r");
	if (!fp)
	{
		settings_delete(settings);
		return NULL;
	}

	char*  line = NULL;
	size_t size = 0;
	while (getline(&line, &size, fp) > 0)
	{
		char sline[size + 1];
		memcpy(sline, line, size);
		free(line);
		line = NULL;
		sline[size] = '\0';
		size = strlen(sline);

		while (isspace(sline[size - 1]))
			sline[--size] = '\0';

		char* value = strstr(sline, "=");
		if (!value) continue;
		*value = '\0';
		value = &value[1];
		while (isspace(*value))
			value = &value[1];

		if (strncmp(sline, "manifest-repo", 13) == 0)
		{
			if (value[0] == '\0')
				continue;
			settings_manifest_repo_set(
				settings, value);
		}
		else if (strncmp(sline, "manifest-name", 13) == 0)
		{
			if (value[0] == '\0')
				continue;
			settings_manifest_name_set(
				settings, value);
		}
		else if (strncmp(sline, "manifest-url", 12) == 0)
		{
			if (value[0] == '\0')
				continue;
			settings_manifest_url_set(
				settings, value);
		}
		else if (strncmp(sline, "mirror", 6) == 0)
		{
			if ((value[0] == '\0')
				|| (value[1] != '\0')
				|| ((value[0] != '0') && (value[0] != '1')))
				continue;
			settings->mirror = (value[0] == '1');
		}
		else if (strncmp(sline, "group-filter", 12) == 0)
		{
			size_t vlen = strlen(value) + 1;
			char* vstr = (char*)malloc(vlen);
			if (!vstr) continue;
			memcpy(vstr, value, vlen);

			settings->group_count = 0;
			free(settings->group);
			settings->group = NULL;
			free(settings->group_string);
			settings->group_string = vstr;

			group_list_parse(
				settings->group_string, true,
				&settings->group,
				&settings->group_count);
		}
	}

	fclose(fp);

	return settings;
}

bool settings_write(settings_t* settings, const char* path)
{
	if ((settings->manifest_repo == manifest_repo_default)
		&& (settings->manifest_name == manifest_name_default)
		&& !settings->mirror
		&& (settings->group_count == 0))
		return true;

	FILE* fp = fopen(path, "w");
	if (!fp) return false;

	if (settings->manifest_repo
		&& (settings->manifest_repo != manifest_repo_default))
		fprintf(fp, "manifest-repo=%s\n", settings->manifest_repo);

	if (settings->manifest_name
		&& (settings->manifest_name != manifest_name_default))
		fprintf(fp, "manifest-name=%s\n", settings->manifest_name);

	if (settings->mirror)
		fprintf(fp, "mirror=%u\n", (unsigned)settings->mirror);

	if (settings->group
		&& (settings->group_count > 0))
	{
		fprintf(fp, "group-filter=");

		unsigned i;
		for (i = 0; i < settings->group_count; i++)
			fprintf(fp, "%s%.*s",
				(i > 0 ? "," : ""),
				settings->group[i].size,
				settings->group[i].name);

		fprintf(fp, "\n");
	}

	fclose(fp);
	return true;
}

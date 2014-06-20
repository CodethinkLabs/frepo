#include "settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>



settings_t* settings_create(bool mirror)
{
	settings_t* settings
		= (settings_t*)malloc(
			sizeof(settings_t));
	if (!settings) return NULL;

	settings->mirror      = mirror;
	settings->group       = NULL;
	settings->group_count = 0;
	settings->group_string  = NULL;
	return settings;
}

void settings_delete(settings_t* settings)
{
	if (!settings)
		return;
	free(settings->group);
	free(settings->group_string);
	free(settings);
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

		if (strncmp(sline, "mirror", 6) == 0)
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
	if (!settings->mirror
		&& (settings->group_count == 0))
		return true;

	FILE* fp = fopen(path, "w");
	if (!fp) return false;

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

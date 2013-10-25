#include "manifest.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>



void manifest_delete(manifest_t* manifest)
{
	if (!manifest)
		return;

	if (manifest->document)
		xml_tag_delete(manifest->document);
	free(manifest);
}

manifest_t* manifest_parse(xml_tag_t* document)
{
	if (!document
		|| (document->tag_count != 1)
		|| (strcmp(document->tag[0]->name, "manifest") != 0))
	{
		fprintf(stderr, "Error: No manifest in XML file.\n");
		return NULL;
	}

	xml_tag_t* mdoc = document->tag[0];

	unsigned remote_count  = 0;
	unsigned project_count = 0;

	unsigned i;
	for (i = 0; i < mdoc->tag_count; i++)
	{
		if (strcmp(mdoc->tag[i]->name, "remote") == 0)
			remote_count++;
		else if (strcmp(mdoc->tag[i]->name, "project") == 0)
			project_count++;
		else if (strcmp(mdoc->tag[i]->name, "default") != 0)
		{
			fprintf(stderr,
				"Error: Unrecognized tag '%s' in manifest.\n",
				mdoc->tag[i]->name);
			return NULL;
		}
	}

	xml_tag_t* remote[remote_count];

	unsigned j;
	for (i = 0, j = 0; i < mdoc->tag_count; i++)
	{
		if (strcmp(mdoc->tag[i]->name, "remote") == 0)
			remote[j++] = mdoc->tag[i];
	}

	manifest_t* manifest = (manifest_t*)malloc(
		sizeof(manifest_t) + (project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->project_count = project_count;
	manifest->project = (project_t*)((uintptr_t)manifest + sizeof(manifest_t));

	xml_tag_t*  default_remote   = (remote_count ? remote[0] : NULL);
	const char* default_revision = NULL;

	for (i = 0, j = 0; i < mdoc->tag_count; i++)
	{
		if (strcmp(mdoc->tag[i]->name, "default") == 0)
		{
			const char* nrevision
				= xml_tag_field(mdoc->tag[i], "revision");
			if (nrevision)
				default_revision = nrevision;

			const char* nremote
				= xml_tag_field(mdoc->tag[i], "remote");
			if (nremote)
			{
				unsigned r;
				for (r = 0; r < remote_count; r++)
				{
					const char* rname
						= xml_tag_field(remote[r], "name");
					if (rname && (strcmp(rname, nremote) == 0))
					{
						default_remote = remote[r];
						break;
					}
				}
				if (r >= remote_count)
				{
					fprintf(stderr,
						"Error: Invalid remote name '%s' in default tag.\n",
						nremote);
					free(manifest);
					return NULL;
				}
			}
		}
		else if (strcmp(mdoc->tag[i]->name, "project") == 0)
		{
			manifest->project[j].path
				= xml_tag_field(mdoc->tag[i], "path");
			manifest->project[j].name
				= xml_tag_field(mdoc->tag[i], "name");

			manifest->project[j].remote
				= xml_tag_field(mdoc->tag[i], "remote");
			if (manifest->project[j].remote)
			{
				unsigned r;
				for (r = 0; r < remote_count; r++)
				{
					const char* rname
						= xml_tag_field(remote[r], "name");
					if (rname
						&& (strcmp(rname, manifest->project[j].remote) == 0))
					{
						manifest->project[j].remote
							= xml_tag_field(remote[r], "fetch");
						manifest->project[j].remote_name
							= xml_tag_field(remote[r], "name");
						break;
					}
				}
				if (r >= remote_count)
				{
					fprintf(stderr,
						"Error: Invalid remote name '%s' in project tag.\n",
						manifest->project[j].remote);
					free(manifest);
					return NULL;
				}
			}
			else
			{
				manifest->project[j].remote
					= xml_tag_field(default_remote, "fetch");
				manifest->project[j].remote_name
					= xml_tag_field(default_remote, "name");
			}

			manifest->project[j].revision
				= xml_tag_field(mdoc->tag[i], "revision");
			if (!manifest->project[j].revision)
				manifest->project[j].revision = default_revision;

			if (!manifest->project[j].path
				|| !manifest->project[j].name
				|| !manifest->project[j].remote
				|| !manifest->project[j].revision)
			{
				fprintf(stderr,
					"Error: Invalid project tag, missing field.\n");
				free(manifest);
				return NULL;
			}

			j++;
		}
	}

	manifest->document = document;
	return manifest;
}

manifest_t* manifest_read(const char* path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		fprintf(stderr, "Error: Failed to open %s.\n", path);
		return NULL;
	}
	struct stat manifest_stat;
	if (fstat(fd, &manifest_stat) < 0)
	{
		close(fd);
		fprintf(stderr, "Error: Failed to stat %s.\n", path);
		return NULL;
	}

	char manifest_string[manifest_stat.st_size + 1];
	if (read(fd, manifest_string, manifest_stat.st_size) < 0)
	{
		close(fd);
		fprintf(stderr, "Error: Failed to read %s.\n", path);
		return NULL;
	}
	manifest_string[manifest_stat.st_size] = '\0';

	close(fd);

	xml_tag_t* manifest_xml
		= xml_document_parse(manifest_string);
	if (!manifest_xml)
	{
		fprintf(stderr, "Error: Failed to parse xml in manifest file.\n");
		return NULL;
	}

	manifest_t* manifest
		= manifest_parse(manifest_xml);
	if (!manifest)
	{
		fprintf(stderr, "Error: Failed to parse manifest from xml document.\n");
		xml_tag_delete(manifest_xml);
		return NULL;
	}

	return manifest;
}



manifest_t* manifest_copy(manifest_t* a)
{
	if (!a) return NULL;

	manifest_t* manifest = (manifest_t*)malloc(
		sizeof(manifest_t) + (a->project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->project_count = a->project_count;
	manifest->project = (project_t*)((uintptr_t)manifest + sizeof(manifest_t));

	unsigned i;
	for (i = 0; i < a->project_count; i++)
		manifest->project[i] = a->project[i];

	return manifest;
}



manifest_t* manifest_subtract(manifest_t* a, manifest_t* b)
{
	if (!a) return NULL;
	if (!b) return manifest_copy(a);

	unsigned project_count = a->project_count;

	unsigned i, j;
	for (i = 0; i < a->project_count; i++)
	{
		for (j = 0; j < b->project_count; j++)
		{
			if (strcmp(a->project[i].path, b->project[j].path) == 0)
				project_count--;
		}
	}

	if (project_count == 0)
		return NULL;

	manifest_t* manifest = (manifest_t*)malloc(
		sizeof(manifest_t) + (project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->project_count = project_count;
	manifest->project = (project_t*)((uintptr_t)manifest + sizeof(manifest_t));

	unsigned k;
	for (i = 0, k = 0; i < a->project_count; i++)
	{
		for (j = 0; j < b->project_count; j++)
		{
			if (strcmp(a->project[i].path, b->project[j].path) == 0)
				break;
		}

		if (j >= b->project_count)
			manifest->project[k++] = a->project[i];
	}

	return manifest;
}

#include "manifest.h"
#include "git.h"

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

	unsigned i;
	for (i = 0; i < manifest->project_count; i++)
		free(manifest->project[i].copyfile);

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

	manifest_t* manifest = (manifest_t*)malloc(
		sizeof(manifest_t)
		+ (remote_count * sizeof(remote_t))
		+ (project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->remote_count = remote_count;
	manifest->remote  = (remote_t*)((uintptr_t)manifest + sizeof(manifest_t));
	manifest->project_count = project_count;
	manifest->project = (project_t*)&manifest->remote[manifest->remote_count];
	manifest->document = NULL;

	unsigned j;
	for (i = 0, j = 0; i < mdoc->tag_count; i++)
	{
		if (strcmp(mdoc->tag[i]->name, "remote") == 0)
		{
			manifest->remote[j].name
				= xml_tag_field(mdoc->tag[i], "name");
			manifest->remote[j].fetch
				= xml_tag_field(mdoc->tag[i], "fetch");
			if (!manifest->remote[j].name
				|| !manifest->remote[j].fetch)
			{
				fprintf(stderr,
					"Error: Missing 'name' or 'fetch' field in remote tag.\n");
				free(manifest);
				return NULL;
			}
			j++;
		}
	}

	remote_t*   default_remote
		= (manifest->remote_count > 0 ? &manifest->remote[0] : NULL);
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
					const char* rname = manifest->remote[r].name;
					if (rname && (strcmp(rname, nremote) == 0))
					{
						default_remote = &manifest->remote[r];
						break;
					}
				}
				if (r >= manifest->remote_count)
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
			project_t* project = &manifest->project[j];

			project->path
				= xml_tag_field(mdoc->tag[i], "path");
			project->name
				= xml_tag_field(mdoc->tag[i], "name");

			project->remote
				= xml_tag_field(mdoc->tag[i], "remote");
			if (project->remote)
			{
				unsigned r;
				for (r = 0; r < remote_count; r++)
				{
					const char* rname = manifest->remote[r].name;
					if (rname
						&& (strcmp(rname, project->remote) == 0))
					{
						project->remote      = manifest->remote[r].fetch;
						project->remote_name = manifest->remote[r].name;
						break;
					}
				}
				if (r >= remote_count)
				{
					fprintf(stderr,
						"Error: Invalid remote name '%s' in project tag.\n",
						project->remote);
					free(manifest);
					return NULL;
				}
			}
			else
			{
				project->remote      = default_remote->fetch;
				project->remote_name = default_remote->name;
			}

			project->revision
				= xml_tag_field(mdoc->tag[i], "revision");
			if (!project->revision)
				project->revision = default_revision;

			project->copyfile_count = 0;
			project->copyfile = NULL;

			unsigned k;
			for (k = 0; k < mdoc->tag[i]->tag_count; k++)
			{
				if (strcmp(mdoc->tag[i]->tag[k]->name, "copyfile") != 0)
				{
					fprintf(stderr,
						"Warning: Unknown project sub-tag '%s'.\n",
						mdoc->tag[i]->tag[k]->name);
					continue;
				}

				copyfile_t* ncopyfile
					= (copyfile_t*)realloc(project->copyfile,
						(project->copyfile_count + 1) * sizeof(copyfile_t));
				if (!ncopyfile)
				{
					fprintf(stderr,
						"Error: Failed to add copyfile to project.\n");
					manifest_delete(manifest);
					return NULL;
				}

				project->copyfile = ncopyfile;
				project->copyfile[project->copyfile_count].source
					= xml_tag_field(mdoc->tag[i]->tag[k], "src");
				project->copyfile[project->copyfile_count].dest
					= xml_tag_field(mdoc->tag[i]->tag[k], "dest");

				if (!project->copyfile[project->copyfile_count].source)
				{
					fprintf(stderr,
						"Error: Invalid copyfile tag, missing source field.\n");
					manifest_delete(manifest);
					return NULL;
				}

				if (!project->copyfile[project->copyfile_count].dest)
				{
					fprintf(stderr,
						"Error: Invalid copyfile tag, missing dest field.\n");
					manifest_delete(manifest);
					return NULL;
				}

				project->copyfile_count++;
			}

			if (!project->path
				|| !project->name
				|| !project->remote
				|| !project->revision)
			{
				fprintf(stderr,
					"Error: Invalid project tag, missing field.\n");
				manifest_delete(manifest);
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
		sizeof(manifest_t)
		+ (a->remote_count * sizeof(remote_t))
		+ (a->project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->remote_count = a->remote_count;
	manifest->remote  = (remote_t*)((uintptr_t)manifest + sizeof(manifest_t));
	manifest->project_count = a->project_count;
	manifest->project = (project_t*)&manifest->remote[manifest->remote_count];
	manifest->document = NULL;

	unsigned i;
	for (i = 0; i < a->remote_count; i++)
		manifest->remote[i] = a->remote[i];

	for (i = 0; i < a->project_count; i++)
	{
		manifest->project[i] = a->project[i];

		manifest->project[i].copyfile = NULL;
		if (a->project[i].copyfile_count)
		{
			manifest->project[i].copyfile
				= (copyfile_t*)malloc(
					a->project[i].copyfile_count * sizeof(copyfile_t));
			if (!manifest->project[i].copyfile)
			{
				unsigned j;
				for (j = i; j < a->project_count; j++)
				{
					manifest->project[j].copyfile = NULL;
					manifest->project[j].copyfile_count = 0;
				}
				manifest_delete(manifest);
				return NULL;
			}
			memcpy(
				manifest->project[i].copyfile,
				a->project[i].copyfile,
				(a->project[i].copyfile_count * sizeof(copyfile_t)));
		}
	}

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
		sizeof(manifest_t)
		+ (a->remote_count * sizeof(remote_t))
		+ (project_count * sizeof(project_t)));
	if (!manifest) return NULL;
	manifest->remote_count = a->remote_count;
	manifest->remote  = (remote_t*)((uintptr_t)manifest + sizeof(manifest_t));
	manifest->project_count = project_count;
	manifest->project = (project_t*)&manifest->remote[manifest->remote_count];
	manifest->document = NULL;

	for (i = 0; i < a->remote_count; i++)
		manifest->remote[i] = a->remote[i];

	unsigned k;
	for (i = 0, k = 0; i < a->project_count; i++)
	{
		for (j = 0; j < b->project_count; j++)
		{
			if (strcmp(a->project[i].path, b->project[j].path) == 0)
				break;
		}

		if (j >= b->project_count)
		{
			manifest->project[k] = a->project[i];
			if (a->project[i].copyfile_count)
			{
				manifest->project[k].copyfile
					= (copyfile_t*)malloc(
						manifest->project[i].copyfile_count * sizeof(copyfile_t));
				if (!manifest->project[k].copyfile)
				{
					unsigned l;
					for (l = k; l < project_count; l++)
					{
						manifest->project[l].copyfile = NULL;
						manifest->project[l].copyfile_count = 0;
					}
					manifest_delete(manifest);
					return NULL;
				}
				memcpy(
					manifest->project[k].copyfile,
					a->project[i].copyfile,
					(a->project[i].copyfile_count * sizeof(copyfile_t)));
			}
			k++;
		}
	}

	return manifest;
}



bool manifest_write_snapshot(manifest_t* manifest, const char* path)
{
	if (!manifest)
		return false;

	FILE* fp = fopen(path, "w");
	if (!fp)
	{
		fprintf(stderr, "Error: Failed to open '%s'"
			" to write manifest snapshot.\n", path);
		return false;
	}

	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(fp, "<manifest>\n");

	unsigned i;
	for (i = 0; i < manifest->remote_count; i++)
	{
		remote_t* remote = &manifest->remote[i];

		fprintf(fp, "\t<remote name=\"%s\" fetch=\"%s\"/>\n",
			remote->name, remote->fetch);
	}

	for (i = 0; i < manifest->project_count; i++)
	{
		project_t* project = &manifest->project[i];

		char* revision
			= git_current_commit(project->path);
		if (!revision)
		{
			fprintf(stderr, "Error: Failed to get current revision"
				" during snapshot.\n");
			fclose(fp);
			return false;
		}

		fprintf(fp, "\t<project path=\"%s\" name=\"%s\""
			" revision=\"%s\" remote=\"%s\"",
			project->path, project->name,
			revision, project->remote_name);

		free(revision);

		if (project->copyfile_count)
		{
			fprintf(fp, ">\n");

			unsigned j;
			for (j = 0; j < project->copyfile_count; j++)
			{
				fprintf(fp, "\t\t<copyfile src=\"%s\" dest=\"%s\"/>\n",
					project->copyfile[j].source, project->copyfile[j].dest);
			}

			fprintf(fp, "\t</project>\n");
		}
		else
		{
			fprintf(fp, "/>\n");
		}
	}

	fprintf(fp, "</manifest>\n");
	bool err = ferror(fp);
	fclose(fp);
	return !err;
}

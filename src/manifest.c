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
	{
		free(manifest->project[i].copyfile);
		free(manifest->project[i].linkfile);
		free(manifest->project[i].group);
	}

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
		else if (strcmp(mdoc->tag[i]->name, "repo-hooks") == 0)
			// Hooks are python functions loaded from file and lose
			// the benefits of a C implementation.
			fprintf(stderr,
				"Warning: repo python hooks are ignored.\n");
		else if (strcmp(mdoc->tag[i]->name, "bugurl") == 0)
			fprintf(stderr,
				"Warning: bugurl is ignored.\n");
		else if (strcmp(mdoc->tag[i]->name, "contactinfo") == 0)
			fprintf(stderr,
				"Warning: contactinfo is ignored.\n");
		else if (strcmp(mdoc->tag[i]->name, "superproject") == 0)
			// Use of superproject to sync can be opted out with
			// the --no-use-superproject option of android git-repo
			// so we behave as if it was always provided.
			fprintf(stderr,
				"Warning: superprojects are not supported.\n");
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
	manifest->threads = 1;

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

			const char* sync_j
				= xml_tag_field(mdoc->tag[i], "sync-j");
			long int threads = strtol(sync_j, NULL, 0);
			if (threads > 0)
				manifest->threads = threads;

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

			if (xml_tag_field(mdoc->tag[i], "clone-depth"))
			{
				static bool warned = false;
				if (!warned)
				{
					fprintf(stderr, "Warning: clone-depth is ignored.\n");
					warned = true;
				}
			}
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

			project->linkfile_count = 0;
			project->linkfile = NULL;

			project->group_count = 0;
			project->group = NULL;

			unsigned k;
			for (k = 0; k < mdoc->tag[i]->tag_count; k++)
			{
				const char* tag_name = mdoc->tag[i]->tag[k]->name;

				const char *copyfile_kind;
				copyfile_t** copyfile;
				unsigned* copyfile_count;
				if (strcmp(tag_name, "copyfile") == 0)
				{
					copyfile_kind = "copyfile";
					copyfile = &project->copyfile;
					copyfile_count = &project->copyfile_count;
				}
				else if (strcmp(tag_name, "linkfile") == 0)
				{
					copyfile_kind = "linkfile";
					copyfile = &project->linkfile;
					copyfile_count = &project->linkfile_count;
				}
				else
				{
					fprintf(stderr,
						"Warning: Unknown project sub-tag '%s'.\n",
						tag_name);
					continue;
				}

				copyfile_t* ncopyfile
					= (copyfile_t*)realloc(*copyfile,
						((*copyfile_count) + 1) * sizeof(copyfile_t));
				if (!ncopyfile)
				{
					fprintf(stderr,
						"Error: Failed to add %s to project.\n",
						copyfile_kind);
					manifest_delete(manifest);
					return NULL;
				}

				*copyfile = ncopyfile;
				(*copyfile)[*copyfile_count].source
					= xml_tag_field(mdoc->tag[i]->tag[k], "src");
				(*copyfile)[*copyfile_count].dest
					= xml_tag_field(mdoc->tag[i]->tag[k], "dest");

				if (!(*copyfile)[*copyfile_count].source)
				{
					fprintf(stderr,
						"Error: Invalid %s tag, missing source field.\n",
						copyfile_kind);
					manifest_delete(manifest);
					return NULL;
				}

				if (!(*copyfile)[*copyfile_count].dest)
				{
					fprintf(stderr,
						"Error: Invalid %s tag, missing dest field.\n",
						copyfile_kind);
					manifest_delete(manifest);
					return NULL;
				}

				(*copyfile_count)++;
			}

			const char* groups
				= xml_tag_field(mdoc->tag[i], "groups");
			if (groups)
			{
				if (!group_list_parse(groups, false,
					&project->group, &project->group_count))
				{
					fprintf(stderr,
						"Error: Failed to parse group list.\n");
						manifest_delete(manifest);
						return NULL;
				}
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
	if (fd < 0) return NULL;

	struct stat manifest_stat;
	if (fstat(fd, &manifest_stat) < 0)
	{
		close(fd);
		fprintf(stderr, "Error: Failed to stat %s.\n", path);
		return NULL;
	}

	if (manifest_stat.st_size < 0)
	{
		close(fd);
		fprintf(stderr, "Error: File %s has negative size.\n", path);
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
		= xml_document_parse(manifest_string, manifest_stat.st_size);
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

// Create a new copyfiles by copying the source copyfiles.
// On success the new copyfiles is returned
// and the length is stored in dst_copyfile_count.
// On failure NULL is returned and dst_copyfile_count is untouched.
copyfile_t* manifest__copyfiles_copy(
	copyfile_t* src_copyfile,
	unsigned src_copyfile_count,
	unsigned* dst_copyfile_count)
{
	copyfile_t* ncopyfile
		= (copyfile_t*)malloc(
			src_copyfile_count * sizeof(copyfile_t));
	if (!ncopyfile) return NULL;

	memcpy(
		ncopyfile,
		src_copyfile,
		(src_copyfile_count * sizeof(copyfile_t)));
	*dst_copyfile_count = src_copyfile_count;
	return ncopyfile;
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
		manifest->project[i].copyfile_count = 0;
		manifest->project[i].linkfile = NULL;
		manifest->project[i].linkfile_count = 0;
		manifest->project[i].group = NULL;
		manifest->project[i].group_count = 0;
	}

	for (i = 0; i < a->project_count; i++)
	{
		if (a->project[i].copyfile_count)
		{
			manifest->project[i].copyfile
				= manifest__copyfiles_copy(
					a->project[i].copyfile,
					a->project[i].copyfile_count,
					&manifest->project[i].copyfile_count);
			if (!manifest->project[i].copyfile)
			{
				manifest_delete(manifest);
				return NULL;
			}
		}
		if (a->project[i].linkfile_count)
		{
			manifest->project[i].linkfile
				= manifest__copyfiles_copy(
					a->project[i].linkfile,
					a->project[i].linkfile_count,
					&manifest->project[i].linkfile_count);
			if (!manifest->project[i].linkfile)
			{
				manifest_delete(manifest);
				return NULL;
			}
		}
		if (a->project[i].group_count)
		{
			if (!group_list_copy(
				a->project[i].group,
				a->project[i].group_count,
				&manifest->project[i].group,
				&manifest->project[i].group_count))
			{
				manifest_delete(manifest);
				return NULL;
			}
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

	for (i = 0; i < project_count; i++)
	{
		manifest->project[i].copyfile = NULL;
		manifest->project[i].copyfile_count = 0;
		manifest->project[i].group = NULL;
		manifest->project[i].group_count = 0;
	}

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
					manifest_delete(manifest);
					return NULL;
				}
				memcpy(
					manifest->project[k].copyfile,
					a->project[i].copyfile,
					(a->project[i].copyfile_count * sizeof(copyfile_t)));
			}
			if (a->project[i].group_count)
			{
				if (!group_list_copy(
					a->project[i].group,
					a->project[i].group_count,
					&manifest->project[k].group,
					&manifest->project[k].group_count))
				{
					manifest_delete(manifest);
					return NULL;
				}
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

		if (project->group_count > 0)
		{
			fprintf(fp, " group=\"");
			unsigned j;
			for (j = 0; j < project->group_count; j++)
			{
				fprintf(fp, "%s%.*s",
					(j > 0 ? "," : ""),
					project->group[j].size,
					project->group[j].name);
			}
			fprintf(fp, "\"");
		}

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

		if (project->linkfile_count)
		{
			fprintf(fp, ">\n");

			unsigned j;
			for (j = 0; j < project->linkfile_count; j++)
			{
				fprintf(fp, "\t\t<linkfile src=\"%s\" dest=\"%s\"/>\n",
					project->linkfile[j].source, project->linkfile[j].dest);
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



manifest_t* manifest_group_filter(
	manifest_t* manifest,
	group_t* filter, unsigned filter_count)
{
	if (!manifest)
		return NULL;

	bool mask[manifest->project_count];

	unsigned i;
	unsigned project_count = 0;
	for (i = 0; i < manifest->project_count; i++)
	{
		project_t* project = &manifest->project[i];
		mask[i] = false;
		// Process the filter in reverse since the result only depends
		// on the last match
		for (group_t* rule = &filter[filter_count - 1];
			rule >= filter;
			rule--)
		{
			// TODO: Should pre-parsing have an enum for rule->kind
			// as ALL, DEFAULT or NAMED to reduce redundant parsing?
			if (strcmp(rule->name, "all") == 0)
			{
				mask[i] = !rule->exclude;
				break;
			}

			bool matched = false;
			if (strcmp(rule->name, "default") == 0)
			{
				// Default implicitly exists if
				// notdefault does not
				if (!group_list_match(
					"notdefault",
					strlen("notdefault"),
					project->group,
					project->group_count,
					NULL))
				{
					mask[i] = !rule->exclude;
					matched = true;
				}
				// Perverse input may include both
				// default and notdefault so we need to
				// fall through to check if default
				// also exists.
			}

			if (group_list_match(
				rule->name,
				rule->size,
				project->group,
				project->group_count,
				NULL))
			{
				mask[i] = !rule->exclude;
				matched = true;
			}

			if (matched) break;
		}

		if (mask[i])
			project_count++;
	}

	manifest_t* filtered = (manifest_t*)malloc(
		sizeof(manifest_t)
		+ (manifest->remote_count * sizeof(remote_t))
		+ (project_count * sizeof(project_t)));
	if (!filtered) return NULL;
	filtered->remote_count = manifest->remote_count;
	filtered->remote  = (remote_t*)((uintptr_t)filtered + sizeof(manifest_t));
	filtered->project_count = project_count;
	filtered->project = (project_t*)&filtered->remote[filtered->remote_count];
	filtered->document = NULL;

	for (i = 0; i < manifest->remote_count; i++)
		filtered->remote[i] = manifest->remote[i];

	unsigned j;
	for (i = 0, j = 0; i < manifest->project_count; i++)
	{
		if (!mask[i])
			continue;

		filtered->project[j] = manifest->project[i];
		filtered->project[j].copyfile = NULL;
		filtered->project[j].copyfile_count = 0;
		filtered->project[j].group = NULL;
		filtered->project[j].group_count = 0;

		j++;
	}

	for (i = 0, j = 0; i < manifest->project_count; i++)
	{
		if (!mask[i])
			continue;

		if (manifest->project[i].copyfile_count)
		{
			filtered->project[j].copyfile = manifest__copyfiles_copy(
				manifest->project[j].copyfile,
				manifest->project[j].copyfile_count,
				&filtered->project[j].copyfile_count);
			if (!filtered->project[j].copyfile)
			{
				manifest_delete(filtered);
				return NULL;
			}
		}

		if (manifest->project[i].linkfile_count)
		{
			filtered->project[j].linkfile = manifest__copyfiles_copy(
				manifest->project[j].linkfile,
				manifest->project[j].linkfile_count,
				&filtered->project[j].linkfile_count);
			if (!filtered->project[j].linkfile)
			{
				manifest_delete(filtered);
				return NULL;
			}
		}

		if (manifest->project[i].group_count)
		{
			if (!group_list_copy(
				manifest->project[i].group,
				manifest->project[i].group_count,
				&filtered->project[j].group,
				&filtered->project[j].group_count))
			{
				manifest_delete(filtered);
				return NULL;
			}
		}

		j++;
	}

	return filtered;
}

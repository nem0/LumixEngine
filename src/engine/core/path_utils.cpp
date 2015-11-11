#pragma once

#include "path_utils.h"
#include "core/string.h"


namespace Lumix
{
namespace PathUtils
{

void normalize(const char* path, char* out, uint32 max_size)
{
	ASSERT(max_size > 0);
	uint32 i = 0;

	bool is_prev_slash = false;

	if (path[0] == '.' && (path[1] == '\\' || path[1] == '/'))
		++path;
	if (path[0] == '\\' || path[0] == '/')
		++path;
	while (*path != '\0' && i < max_size)
	{
		bool is_current_slash = *path == '\\' || *path == '/';

		if (is_current_slash && is_prev_slash)
		{
			++path;
			continue;
		}

		*out = *path == '\\' ? '/' : *path;
		*out = *path >= 'A' && *path <= 'Z' ? *path - 'A' + 'a' : *out;

		path++;
		out++;
		i++;

		is_prev_slash = is_current_slash;
	}
	(i < max_size ? *out : *(out - 1)) = '\0';
}

void getDir(char* dir, int max_length, const char* src)
{
	copyString(dir, max_length, src);
	for (int i = (int)strlen(dir) - 1; i >= 0; --i)
	{
		if (dir[i] == '\\' || dir[i] == '/')
		{
			++i;
			dir[i] = '\0';
			return;
		}
	}
	dir[0] = '\0';
}

void getBasename(char* basename, int /*max_length*/, const char* src)
{
	basename[0] = '\0';
	for (int i = (int)strlen(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/' || i == 0)
		{
			if (i != 0)
				++i;
			int j = 0;
			basename[j] = src[i];
			while (src[i + j] && src[i + j] != '.')
			{
				++j;
				basename[j] = src[j + i];
			}
			basename[j] = '\0';
			return;
		}
	}
}

void getFilename(char* filename, int /*max_length*/, const char* src)
{
	for (int i = (int)strlen(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/')
		{
			++i;
			strcpy(filename, src + i);
			break;
		}
	}
}


void getExtension(char* extension, int max_length, const char* src)
{
	ASSERT(max_length > 0);
	for (int i = (int)strlen(src) - 1; i >= 0; --i)
	{
		if (src[i] == '.')
		{
			++i;
			copyString(extension, max_length, src + i);
			return;
		}
	}
	extension[0] = '\0';
}


bool hasExtension(const char* filename, const char* ext)
{
	char tmp[20];
	getExtension(tmp, sizeof(tmp), filename);

	return strcmp(tmp, ext) == 0;
}


bool isAbsolute(const char* path)
{
	return path[0] != '\0' && path[1] == ':';
}
}
}
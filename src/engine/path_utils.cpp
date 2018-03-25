#include "path_utils.h"
#include "engine/string.h"


namespace Lumix
{
namespace PathUtils
{

void normalize(const char* path, char* out, u32 max_size)
{
	ASSERT(max_size > 0);
	u32 i = 0;

	bool is_prev_slash = false;

	if (path[0] == '.' && (path[1] == '\\' || path[1] == '/'))
		++path;
	#ifdef _WIN32
		if (path[0] == '\\' || path[0] == '/')
			++path;
	#endif
	while (*path != '\0' && i < max_size)
	{
		bool is_current_slash = *path == '\\' || *path == '/';

		if (is_current_slash && is_prev_slash)
		{
			++path;
			continue;
		}

		*out = *path == '\\' ? '/' : *path;
		#ifdef _WIN32
			*out = *path >= 'A' && *path <= 'Z' ? *path - 'A' + 'a' : *out;
		#endif

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
	for (int i = stringLength(dir) - 1; i >= 0; --i)
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

void getBasename(char* basename, int max_length, const char* src)
{
	basename[0] = '\0';
	for (int i = stringLength(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/' || i == 0)
		{
			if (src[i] == '\\' || src[i] == '/')
				++i;
			int j = 0;
			basename[j] = src[i];
			while (j < max_length - 1 && src[i + j] && src[i + j] != '.')
			{
				++j;
				basename[j] = src[j + i];
			}
			basename[j] = '\0';
			return;
		}
	}
}

void getFilename(char* filename, int max_length, const char* src)
{
	for (int i = stringLength(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/')
		{
			++i;
			copyString(filename, max_length, src + i);
			return;
		}
	}
	copyString(filename, max_length, src);
}


void getExtension(char* extension, int max_length, const char* src)
{
	ASSERT(max_length > 0);
	for (int i = stringLength(src) - 1; i >= 0; --i)
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


bool replaceExtension(char* path, const char* ext)
{
	char* end = path + stringLength(path);
	while (end > path && *end != '.')
	{
		--end;
	}
	if (*end != '.') return false;

	++end;
	const char* src = ext;
	while (*src != '\0' && *end != '\0')
	{
		*end = *src;
		++end;
		++src;
	}
	bool copied_whole_ext = *src == '\0';
	if (!copied_whole_ext) return false;

	*end = '\0';
	return true;
}


bool hasExtension(const char* filename, const char* ext)
{
	char tmp[20];
	getExtension(tmp, lengthOf(tmp), filename);
	makeLowercase(tmp, lengthOf(tmp), tmp);

	return equalStrings(tmp, ext);
}


bool isAbsolute(const char* path)
{
	return path[0] != '\0' && path[1] == ':';
}
}
}

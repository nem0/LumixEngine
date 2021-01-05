#include "engine/lumix.h"
#include "engine/path.h"

#include "engine/crc32.h"
#include "engine/hash_map.h"
#include "engine/sync.h"
#include "engine/path.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{


Path::Path()
	: m_hash(0)
	, m_path{}
{}

//Path::Path(const Path& rhs)
//	: m_hash(rhs.m_hash)
//{
//	copyString(m_path, rhs.m_path);
//}

Path::Path(const char* path) {
	normalize(path, Span(m_path));
	m_hash = crc32(m_path);
}

i32 Path::length() const {
	return stringLength(m_path);
}

void Path::operator =(const char* rhs) {
	normalize(rhs, Span(m_path));
	m_hash = crc32(m_path);
}

bool Path::operator==(const Path& rhs) const {
	return m_hash == rhs.m_hash;
}

bool Path::operator!=(const Path& rhs) const {
	return m_hash != rhs.m_hash;
}

void Path::normalize(const char* path, Span<char> output)
{
	char* out = output.begin();
	u32 max_size = output.length();
	ASSERT(max_size > 0);
	u32 i = 0;

	bool is_prev_slash = false;

	if (path[0] == '.' && (path[1] == '\\' || path[1] == '/'))
		path += 2;
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

void Path::getDir(Span<char> dir, const char* src)
{
	copyString(dir, src);
	for (int i = stringLength(dir.begin()) - 1; i >= 0; --i)
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

void Path::getBasename(Span<char> basename, const char* src)
{
	basename[0] = '\0';
	for (int i = stringLength(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/' || i == 0)
		{
			if (src[i] == '\\' || src[i] == '/')
				++i;
			u32 j = 0;
			basename[j] = src[i];
			while (j < basename.length() - 1 && src[i + j] && src[i + j] != '.')
			{
				++j;
				basename[j] = src[j + i];
			}
			basename[j] = '\0';
			return;
		}
	}
}


void Path::getExtension(Span<char> extension, Span<const char> src)
{
	ASSERT(extension.length() > 0);
	for (int i = src.length() - 1; i >= 0; --i)
	{
		if (src[i] == '.')
		{
			++i;
			Span<const char> tmp = { src.begin() + i, src.end() };
			copyString(extension, tmp);
			return;
		}
	}
	extension[0] = '\0';
}


bool Path::replaceExtension(char* path, const char* ext)
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


bool Path::hasExtension(const char* filename, const char* ext)
{
	char tmp[20];
	getExtension(Span(tmp), Span(filename, stringLength(filename)));
	makeLowercase(Span(tmp), tmp);

	return equalStrings(tmp, ext);
}

PathInfo::PathInfo(const char* path) {
	char tmp[LUMIX_MAX_PATH];
	Path::normalize(path, Span(tmp));
	Path::getExtension(Span(m_extension), Span(tmp, stringLength(tmp)));
	Path::getBasename(Span(m_basename), tmp);
	Path::getDir(Span(m_dir), tmp);
}



} // namespace Lumix

#include "engine/lumix.h"
#include "engine/path.h"

#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/sync.h"
#include "engine/path.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{


Path::Path()
	: m_path{}
{}


Path::Path(const char* path) {
	normalize(path, Span(m_path));
	#ifdef _WIN32
		char tmp[LUMIX_MAX_PATH];
		makeLowercase(Span(tmp), m_path);
		m_hash = StableHash32(tmp);
	#else
		m_hash = StableHash32(m_path);
	#endif
}

i32 Path::length() const {
	return stringLength(m_path);
}

void Path::operator =(const char* rhs) {
	normalize(rhs, Span(m_path));
	#ifdef _WIN32
		char tmp[LUMIX_MAX_PATH];
		makeLowercase(Span(tmp), m_path);
		m_hash = StableHash32(tmp);
	#else
		m_hash = StableHash32(m_path);
	#endif
}

bool Path::operator==(const Path& rhs) const {
	ASSERT(equalIStrings(m_path, rhs.m_path) == (m_hash == rhs.m_hash));
	return m_hash == rhs.m_hash;
}

bool Path::operator!=(const Path& rhs) const {
	ASSERT(equalIStrings(m_path, rhs.m_path) == (m_hash == rhs.m_hash));
	#ifdef _WIN32
		return m_hash != rhs.m_hash || !equalIStrings(m_path, rhs.m_path);
	#else
		return m_hash != rhs.m_hash || !equalStrings(m_path, rhs.m_path);
	#endif
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

		path++;
		out++;
		i++;

		is_prev_slash = is_current_slash;
	}
	(i < max_size ? *out : *(out - 1)) = '\0';
}

Span<const char> Path::getDir(const char* src)
{
	if (!src[0]) return {nullptr, nullptr};
	
	Span<const char> dir;
	dir.m_begin = src;
	dir.m_end = src + stringLength(src) - 1;
	while (dir.m_end != dir.m_begin && *dir.m_end != '\\' && *dir.m_end != '/') {
		--dir.m_end;
	}
	if (dir.m_end != dir.m_begin) ++dir.m_end;
	return dir;
}

Span<const char> Path::getBasename(const char* src)
{
	if (!src[0]) return Span<const char>(src, src);

	Span<const char> res;
	const char* end = src + stringLength(src);
	res.m_end = end;
	res.m_begin = end;
	while (res.m_begin != src && *res.m_begin != '\\' && *res.m_begin != '/') {
		--res.m_begin;
	}

	if (*res.m_begin == '\\' || *res.m_begin == '/') ++res.m_begin;
	res.m_end = res.m_begin;

	while (res.m_end != end && *res.m_end != '.') ++res.m_end;

	return res;
}


Span<const char> Path::getExtension(Span<const char> src)
{
	if (src.length() == 0) return src;

	Span<const char> res;
	res.m_end = src.m_end;
	res.m_begin = src.m_end - 1;

	while(res.m_begin != src.m_begin && *res.m_begin != '.') {
		--res.m_begin;
	}
	if (*res.m_begin != '.') return Span<const char>(nullptr, nullptr);
	++res.m_begin;
	return res;
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
	copyString(Span(tmp), getExtension(Span(filename, stringLength(filename))));

	return equalIStrings(tmp, ext);
}

PathInfo::PathInfo(const char* path) {
	char tmp[LUMIX_MAX_PATH];
	Path::normalize(path, Span(tmp));
	copyString(Span(m_extension), Path::getExtension(Span(tmp, stringLength(tmp))));
	copyString(Span(m_basename), Path::getBasename(tmp));
	copyString(Span(m_dir), Path::getDir(tmp));
}



} // namespace Lumix

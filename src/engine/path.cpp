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


Path::Path(StringView path) {
	normalize(path, Span(m_path));
	endUpdate();
}

void Path::add(StringView value) {
	char tmp[LUMIX_MAX_PATH];
	copyString(tmp, m_path);
	catString(tmp, value);
	normalize(tmp, Span(m_path));
}

void Path::add(StableHash hash) {
	char tmp[32];
	toCString(hash.getHashValue(), Span(tmp));
	catString(m_path, tmp);
}

void Path::add(u64 value) {
	char tmp[32];
	toCString(value, Span(tmp));
	catString(m_path, tmp);
}

i32 Path::length() const {
	return stringLength(m_path);
}

void Path::endUpdate() {
	#ifdef _WIN32
		char tmp[LUMIX_MAX_PATH];
		makeLowercase(Span(tmp), m_path);
		m_hash = FilePathHash(tmp);
	#else
		m_hash = FilePathHash(m_path);
	#endif
}

void Path::operator =(const char* rhs) {
	if (rhs == m_path) return;

	normalize(rhs, Span(m_path));
	endUpdate();
}

bool Path::operator==(const char* rhs) const {
	return equalStrings(rhs, m_path);
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

void Path::normalize(StringView path, Span<char> output) {
	u32 max_size = output.length();
	ASSERT(max_size > 0);
	char* out = output.begin();
	
	path.ensureEnd();
	if (path.size() == 0) {
		*out = '\0';
		return;
	}

	u32 i = 0;
	bool is_prev_slash = false;

	const char* c = path.begin;
	if (c[0] == '.' && path.size() > 1 && (c[1] == '\\' || c[1] == '/')) c += 2;
	
	#ifdef _WIN32
		if (c != path.end && (c[0] == '\\' || c[0] == '/')) ++c;
	#endif

	while (c != path.end && i < max_size) {
		bool is_current_slash = *c == '\\' || *c == '/';

		if (is_current_slash && is_prev_slash) {
			++c;
			continue;
		}

		*out = *c == '\\' ? '/' : *c;

		c++;
		out++;
		i++;

		is_prev_slash = is_current_slash;
	}
	ASSERT(i < max_size);
	(i < max_size ? *out : *(out - 1)) = '\0';
}

StringView Path::getDir(StringView src) {
	if (src.empty()) return src;
	
	src.ensureEnd();
	StringView dir = src;
	dir.removeSuffix(1);

	while (dir.end != dir.begin && *dir.end != '\\' && *dir.end != '/') {
		--dir.end;
	}
	if (dir.end != dir.begin) ++dir.end;
	return dir;
}

StringView Path::getBasename(StringView src) {
	src.ensureEnd();
	
	if (src.size() == 0) return src;
	if (src.back() == '/' || src.back() == '\\') src.removeSuffix(1);

	StringView res;
	const char* end = src.end;
	res.end = end;
	res.begin = end - 1;
	while (res.begin != src.begin && *res.begin != '\\' && *res.begin != '/') {
		--res.begin;
	}

	if (*res.begin == '\\' || *res.begin == '/') ++res.begin;
	res.end = res.begin;

	while (res.end != end && *res.end != '.') ++res.end;

	return res;
}

StringView Path::getExtension(StringView src) {
	src.ensureEnd();
	
	if (src.size() == 0) return src;

	StringView res;
	res.end = src.end;
	res.begin = src.end - 1;

	while(res.begin != src.begin && *res.begin != '.') {
		--res.begin;
	}
	if (*res.begin != '.') return StringView(nullptr, nullptr);
	++res.begin;
	return res;
}

StringView Path::getResource(StringView str) {
	const char* c = str.begin;
	if (str.end) {
		while (c != str.end) {
			if (*c == ':') return StringView(c + 1, str.end);
			++c;
		}
		return str;
	}
	while (*c) {
		if (*c == ':') return c + 1;
		++c;
	}
	return str;
}

StringView Path::getSubresource(StringView str) {
	StringView ret;
	ret.begin = str.begin;
	ret.end = str.begin;
	if (str.end) {
		while(ret.end != str.end && *ret.end != ':') ++ret.end;
	}
	else {
		while(*ret.end && *ret.end != ':') ++ret.end;
	}
	return ret;
}

bool Path::isSame(StringView a, StringView b) {
	a.ensureEnd();
	b.ensureEnd();
	if (a.size() > 0 && (a.back() == '\\' || a.back() == '/')) --a.end;
	if (b.size() > 0 && (b.back() == '\\' || b.back() == '/')) --b.end;
	if (a.size() == 0 && b.size() == 1 && b[0] == '.') return true; 
	if (b.size() == 0 && a.size() == 1 && a[0] == '.') return true; 
	return equalStrings(a, b);
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

bool Path::hasExtension(StringView filename, StringView ext) {
	return equalIStrings(getExtension(filename), ext);
}

Path::operator StringView() const {
	return StringView(m_path);
}

Path::operator const char*() const { return m_path; }

PathInfo::PathInfo(StringView path) {
	Path tmp(path);
	copyString(Span(m_extension), Path::getExtension(tmp));
	copyString(Span(m_basename), Path::getBasename(tmp));
	copyString(Span(m_dir), Path::getDir(tmp));
}



} // namespace Lumix

#include "core/path.h"

namespace Lumix {

Path::Path() : m_path{} {}

Path::Path(StringView path) {
	m_length = u32(normalize(path, Span(m_path)) - m_path);
	m_hash = StableHash(m_path);
}

void Path::add(StringView value) {
	Span span(m_path + m_length, m_path + MAX_PATH);
	m_length = u32(copyString(span, value) - m_path);
}

void Path::add(StableHash hash) {
	Span span(m_path + m_length, m_path + MAX_PATH);
	m_length = u32(toCString(hash.getHashValue(), span) - m_path);
}

void Path::add(u64 value) {
	Span span(m_path + m_length, m_path + MAX_PATH);
	m_length = u32(toCString(value, span) - m_path);
}

char* Path::normalize(char* path) {
	bool is_prev_slash = false;

	char* dst = path;
	const char* src = dst;
	if (src[0] == '.' && (src[1] == '\\' || src[1] == '/')) src += 2;
	
	#ifdef _WIN32
		while (src[0] == '\\' || src[0] == '/') ++src;
	#endif

	while (*src) {
		bool is_current_slash = *src == '\\' || *src == '/';

		if (is_current_slash && is_prev_slash) {
			++src;
			continue;
		}

		*dst = *src == '\\' ? '/' : *src;

		src++;
		dst++;

		is_prev_slash = is_current_slash;
	}
	*dst = 0;
	return dst;
}

void Path::endUpdate() {
	m_length = u32(normalize(m_path) - m_path);
	m_hash = StableHash(m_path);
}

void Path::operator=(StringView rhs) {
	ASSERT(rhs.size() < lengthOf(m_path));
	m_length = u32(normalize(rhs, Span(m_path)) - m_path);
	m_hash = StableHash(m_path); 
}

bool Path::operator==(const char* rhs) const {
	return equalStrings(rhs, m_path);
}

bool Path::operator!=(const char* rhs) const {
	return !equalStrings(rhs, m_path);
}

bool Path::operator==(const Path& rhs) const {
	ASSERT(equalStrings(m_path, rhs.m_path) == (m_hash == rhs.m_hash));
	return m_hash == rhs.m_hash;
}

bool Path::operator!=(const Path& rhs) const {
	ASSERT(equalStrings(m_path, rhs.m_path) == (m_hash == rhs.m_hash));
	return m_hash != rhs.m_hash;
}

char* Path::normalize(StringView path, Span<char> output) {
	ASSERT(path.data <= output.begin() || path.data > output.end());
	u32 max_size = output.length();
	ASSERT(max_size > 0);
	char* out = output.begin();
	
	// empty path
	if (path.size() == 0) {
		*out = '\0';
		return out;
	}

	u32 i = 0;
	bool is_prev_slash = false;

	const char* c = path.data;
	const char* path_end = path.end();
	// skip "./" or ".\"
	if (c[0] == '.' && path.size() > 1 && (c[1] == '\\' || c[1] == '/')) c += 2;

	#ifdef _WIN32
		// skip slashes at the beginning
		if (c != path_end && (c[0] == '\\' || c[0] == '/')) ++c;
	#endif

	while (c != path_end && i < max_size) {
		bool is_current_slash = *c == '\\' || *c == '/';

		if (is_current_slash && is_prev_slash) {
			// skip double slashes
			++c;
			continue;
		}

		// replace "\" with "/"
		*out = *c == '\\' ? '/' : *c;

		c++;
		out++;
		i++;

		is_prev_slash = is_current_slash;
	}
	ASSERT(i < max_size);
	if (i < max_size) {
		*out = '\0';
		return out;
	}
	*(out - 1) = '\0';
	return out - 1;
}

StringView Path::getDir(StringView src) {
	if (src.empty()) return src;

	const char* dir_end = src.end() - 1;
	while (dir_end > src.data && dir_end[-1] != '\\' && dir_end[-1] != '/') --dir_end;
	return StringView(src.data, dir_end);
}

StringView Path::getBasename(StringView src) {
	if (src.empty()) return src;
	if (src.back() == '/' || src.back() == '\\') src.removeSuffix(1);

	StringView res;
	const char* end = src.end();
	res.data = end - 1;
	while (res.data != src.data && *res.data != '\\' && *res.data != '/') {
		--res.data;
	}

	if (*res.data == '\\' || *res.data == '/') ++res.data;
	const char* basename_end = res.data;
	while (basename_end != end && *basename_end != '.') ++basename_end;
	res.length = basename_end - res.data;

	return res;
}

StringView Path::getExtension(StringView src) {
	if (src.empty()) return src;

	StringView res;
	res.data = src.end() - 1;

	while(res.data != src.data && *res.data != '.') {
		--res.data;
	}
	if (*res.data != '.') return StringView(nullptr, nullptr);
	++res.data;
	res.length = src.end() - res.data;
	return res;
}

bool Path::isSame(StringView a, StringView b) {
	if (a.length > 0 && (a.back() == '\\' || a.back() == '/')) --a.length;
	if (b.length > 0 && (b.back() == '\\' || b.back() == '/')) --b.length;
	if (a.length == 0 && b.length == 1 && b[0] == '.') return true; 
	if (b.length == 0 && a.length == 1 && a[0] == '.') return true; 
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
	return StringView(m_path, m_length);
}

PathInfo::PathInfo(StringView path) {
	extension = Path::getExtension(path);
	basename = Path::getBasename(path);
	dir = Path::getDir(path);
}



} // namespace Lumix
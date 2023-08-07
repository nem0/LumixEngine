#pragma once

#include "engine/hash.h"
#include "engine/string.h"


namespace Lumix {

struct StringView;

struct LUMIX_ENGINE_API PathInfo {
	explicit PathInfo(StringView path);

	char m_extension[10];
	char m_basename[LUMIX_MAX_PATH];
	char m_dir[LUMIX_MAX_PATH];
};

struct LUMIX_ENGINE_API Path {
	static void normalize(StringView path, Span<char> out);
	static StringView getDir(StringView src);
	static StringView getBasename(StringView src);
	static StringView getExtension(StringView src);
	static StringView getSubresource(StringView str);
	static StringView getResource(StringView str);
	static bool hasExtension(StringView filename, StringView ext);
	static bool replaceExtension(char* path, const char* ext);
	static bool isSame(StringView a, StringView b);

	Path();
	explicit Path(StringView path);
	template <typename... Args> explicit Path(Args... args);

	void operator=(const char* rhs);
	bool operator==(const char* rhs) const;
	bool operator==(const Path& rhs) const;
	bool operator!=(const Path& rhs) const;

	i32 length() const;
	FilePathHash getHash() const { return m_hash; }
	template <typename... Args> void append(Args... args);
	char* beginUpdate() { return m_path; }
	void endUpdate();
	const char* c_str() const { return m_path; }
	bool isEmpty() const { return m_path[0] == '\0'; }
	static u32 capacity() { return LUMIX_MAX_PATH; }
	operator StringView() const;
	operator const char*() const;

private:
	void add(StringView);
	void add(StableHash hash);
	void add(u64 value);

	char m_path[LUMIX_MAX_PATH];
	FilePathHash m_hash;
};


template <typename... Args> Path::Path(Args... args) {
	m_path[0] = '\0';
	int tmp[] = { (add(args), 0)... };
	(void)tmp;
	endUpdate();
}

template <typename... Args> void Path::append(Args... args) {
	int tmp[] = { (add(args), 0)... };
	(void)tmp;
	endUpdate();
}


} // namespace Lumix

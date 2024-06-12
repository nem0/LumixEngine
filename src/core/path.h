#pragma once

#include "core.h"
#include "hash.h"
#include "string.h"


namespace Lumix {

struct StringView;

struct LUMIX_CORE_API PathInfo {
	explicit PathInfo(StringView path);

	StringView extension;
	StringView basename;
	StringView dir;
};


struct LUMIX_CORE_API Path {
	static char* normalize(StringView in_path, Span<char> out_normalized);
	static char* normalize(char* in_out_path);
	static StringView getDir(StringView src);
	static StringView getBasename(StringView src);
	static StringView getExtension(StringView src);
	static bool hasExtension(StringView filename, StringView ext);
	static bool replaceExtension(char* path, const char* ext);
	static bool isSame(StringView a, StringView b);

	Path();
	explicit Path(StringView path);
	template <typename... Args> explicit Path(Args... args);

	void operator=(StringView rhs);
	bool operator==(const char* rhs) const;
	bool operator==(const Path& rhs) const;
	bool operator!=(const char* rhs) const;
	bool operator!=(const Path& rhs) const;

	u32 length() const { return m_length; }
	FilePathHash getHash() const { return m_hash; }
	template <typename... Args> void append(Args... args);
	char* beginUpdate() { return m_path; }
	void endUpdate();
	const char* c_str() const { return m_path; }
	bool isEmpty() const { return m_path[0] == '\0'; }
	static u32 capacity() { return MAX_PATH; }
	operator StringView() const;

private:
	void add(StringView);
	void add(StableHash hash);
	void add(u64 value);

	char m_path[MAX_PATH];
	u32 m_length = 0;
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

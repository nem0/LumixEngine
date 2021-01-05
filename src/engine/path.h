#pragma once

#include "engine/lumix.h"


namespace Lumix
{

struct LUMIX_ENGINE_API PathInfo
{
	explicit PathInfo(const char* path);

	char m_extension[10];
	char m_basename[LUMIX_MAX_PATH];
	char m_dir[LUMIX_MAX_PATH];
};

struct LUMIX_ENGINE_API Path {
	static void normalize(const char* path, Span<char> out);
	static void getDir(Span<char> dir, const char* src);
	static void getBasename(Span<char> basename, const char* src);
	static void getExtension(Span<char> extension, Span<const char> src);
	static bool hasExtension(const char* filename, const char* ext);
	static bool replaceExtension(char* path, const char* ext);

	Path();
	explicit Path(const char* path);

	void operator=(const char* rhs);
	bool operator==(const Path& rhs) const;
	bool operator!=(const Path& rhs) const;

	i32 length() const;
	u32 getHash() const { return m_hash; }
	const char* c_str() const { return m_path; }
	bool isValid() const { return m_path[0] != '\0'; }

private:
	char m_path[LUMIX_MAX_PATH];
	u32 m_hash;
};


} // namespace Lumix

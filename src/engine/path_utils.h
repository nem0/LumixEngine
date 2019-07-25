#pragma once


#include "engine/lumix.h"


namespace Lumix
{
namespace PathUtils
{
	LUMIX_ENGINE_API void normalize(const char* path, Span<char> out);
	LUMIX_ENGINE_API void getDir(Span<char> dir, const char* src);
	LUMIX_ENGINE_API void getBasename(Span<char> basename, const char* src);
	LUMIX_ENGINE_API void getExtension(Span<char> extension, const char* src);
	LUMIX_ENGINE_API bool hasExtension(const char* filename, const char* ext);
	LUMIX_ENGINE_API bool replaceExtension(char* path, const char* ext);


	struct LUMIX_ENGINE_API FileInfo
	{
		explicit FileInfo(const char* path)
		{
			char tmp[MAX_PATH_LENGTH];
			normalize(path, Span(tmp));
			getExtension(Span(m_extension), tmp);
			getBasename(Span(m_basename), tmp);
			getDir(Span(m_dir), tmp);
		}

		char m_extension[10];
		char m_basename[MAX_PATH_LENGTH];
		char m_dir[MAX_PATH_LENGTH];
	};
} // namespace PathUtils
} // namespace Lumix
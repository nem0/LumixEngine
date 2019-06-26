#pragma once


#include "engine/lumix.h"


namespace Lumix
{
namespace PathUtils
{
	LUMIX_ENGINE_API void normalize(const char* path, char* out, u32 max_size);
	LUMIX_ENGINE_API void getDir(char* dir, int max_length, const char* src);
	LUMIX_ENGINE_API void getBasename(char* basename, int max_length, const char* src);
	LUMIX_ENGINE_API void getExtension(char* extension, int max_length, const char* src);
	LUMIX_ENGINE_API bool hasExtension(const char* filename, const char* ext);
	LUMIX_ENGINE_API bool replaceExtension(char* path, const char* ext);


	struct LUMIX_ENGINE_API FileInfo
	{
		explicit FileInfo(const char* path)
		{
			char tmp[MAX_PATH_LENGTH];
			normalize(path, tmp, lengthOf(tmp));
			getExtension(m_extension, sizeof(m_extension), tmp);
			getBasename(m_basename, sizeof(m_basename), tmp);
			getDir(m_dir, sizeof(m_dir), tmp);
		}

		char m_extension[10];
		char m_basename[MAX_PATH_LENGTH];
		char m_dir[MAX_PATH_LENGTH];
	};
} // namespace PathUtils
} // namespace Lumix
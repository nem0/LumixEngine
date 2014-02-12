#pragma once

namespace Lux
{
	struct LUX_CORE_API PathUtils
	{
		static void normalize(const char* path, char* out, uint32_t max_size)
		{
			uint32_t i = 0;
			while (*path != '\0' && i < max_size)
			{
				*out = *path == '/' ? '\\' : *path;
				*out = *path >= 'A' && *path <= 'Z' ? *path - 'a' : *path;

				path++;
				out++;
				i++;
			}
		}

	private:
		PathUtils();
		~PathUtils();
	};
}
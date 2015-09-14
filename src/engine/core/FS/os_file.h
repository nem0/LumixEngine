#pragma once

#include "lumix.h"
#include "core/fs/ifile_system_defines.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class LUMIX_ENGINE_API OsFile
		{
		public:
			OsFile();
			~OsFile();

			bool open(const char* path, Mode mode, IAllocator& allocator);
			void close();

			bool write(const void* data, size_t size);
			bool read(void* data, size_t size);

			size_t size();
			size_t pos();

			size_t seek(SeekMode base, size_t pos);
			void writeEOF();

			static bool deleteFile(const char* path);
			static bool moveFile(const char* from, const char* to);
			static bool fileExists(const char* path);
			static bool getOpenFilename(char* out, int max_size, const char* filter);
			static bool getOpenDirectory(char* out, int max_size);

		private:
			struct OsFileImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix

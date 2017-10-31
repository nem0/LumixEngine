#pragma once

#include "engine/lumix.h"
#include "engine/fs/file_system.h"

namespace Lumix
{
	struct IAllocator;

	namespace FS
	{
		class LUMIX_ENGINE_API OsFile
		{
		public:
			OsFile();
			~OsFile();

			bool open(const char* path, Mode mode);
			void close();
			void flush();

			bool write(const void* data, size_t size);
			bool writeText(const char* text);
			bool read(void* data, size_t size);

			size_t size();
			size_t pos();

			bool seek(SeekMode base, size_t pos);

			OsFile& operator <<(const char* text);
			OsFile& operator <<(char c) { write(&c, sizeof(c)); return *this; }
			OsFile& operator <<(i32 value);
			OsFile& operator <<(u32 value);
			OsFile& operator <<(u64 value);
			OsFile& operator <<(float value);

			static bool fileExists(const char* path);

		private:
			void* m_handle;
		};
	} // ~namespace FS
} // ~namespace Lumix

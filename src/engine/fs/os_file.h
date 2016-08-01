#pragma once

#include "engine/lumix.h"
#include "engine/fs/file_system.h"

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
			void flush();

			bool write(const void* data, size_t size);
			bool writeText(const char* text);
			bool read(void* data, size_t size);

			size_t size();
			size_t pos();

			bool seek(SeekMode base, size_t pos);

			OsFile& operator <<(const char* text);
			OsFile& operator <<(char c) { write(&c, sizeof(c)); return *this; }
			OsFile& operator <<(int32 value);
			OsFile& operator <<(uint32 value);
			OsFile& operator <<(uint64 value);
			OsFile& operator <<(float value);

			static bool fileExists(const char* path);

		private:
			struct OsFileImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix

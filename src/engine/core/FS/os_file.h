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
			void flush();

			bool write(const void* data, size_t size);
			bool writeText(const char* text);
			bool read(void* data, size_t size);

			size_t size();
			size_t pos();

			size_t seek(SeekMode base, size_t pos);
			void writeEOF();

			OsFile& operator <<(const char* text);
			OsFile& operator <<(int32 value);
			OsFile& operator <<(uint32 value);
			OsFile& operator <<(uint64 value);
			OsFile& operator <<(float value);

		private:
			struct OsFileImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix

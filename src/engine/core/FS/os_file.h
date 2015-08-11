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

		private:
			struct OsFileImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix

#pragma once

#include "core/lux.h"
#include "core/ifile_system_defines.h"

namespace Lux
{
	namespace FS
	{
		class LUX_CORE_API OsFile
		{
		public:
			OsFile();
			~OsFile();

			bool open(const char* path, Mode mode);
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
} // ~namespace Lux

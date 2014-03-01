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

			bool write(const void* data, intptr_t size);
			bool read(void* data, intptr_t size);

			intptr_t size();
			intptr_t pos();

			intptr_t seek(SeekMode base, intptr_t pos);
			void writeEOF();

		private:
			struct OsFileImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lux

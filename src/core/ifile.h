#pragma once

#include "core/ifile_system_defines.h"
#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class IFile LUX_ABSTRACT
		{
		public:
			IFile() {}
			virtual ~IFile() {}

			virtual bool open(const char* path, Mode mode) = 0;
			virtual void close() = 0;

			virtual bool read(void* buffer, intptr_t size) = 0;
			virtual bool write(const void* buffer, intptr_t size) = 0;

			virtual const void* getBuffer() const = 0;
			virtual intptr_t size() = 0;

			virtual intptr_t seek(SeekMode base, intptr_t pos) = 0;
			virtual intptr_t pos() = 0;
		};

	} // ~namespace FS
} // ~namespace Lux
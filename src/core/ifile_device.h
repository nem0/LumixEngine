#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class LUX_CORE_API IFile;
		class LUX_CORE_API IFileDevice
		{
		public:
			IFileDevice() {}
			~IFileDevice() {}

			virtual IFile* createFile(IFile* child) = 0;

			virtual const char* name() const = 0;
		};
	} // ~namespace FS
} // ~namespace Lux
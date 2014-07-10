#pragma once

#include "core/lumix.h"

namespace Lumix
{
	namespace FS
	{
		class LUMIX_CORE_API IFile;
		class LUMIX_CORE_API IFileDevice
		{
		public:
			IFileDevice() {}
			~IFileDevice() {}

			virtual IFile* createFile(IFile* child) = 0;

			virtual const char* name() const = 0;
		};
	} // ~namespace FS
} // ~namespace Lumix

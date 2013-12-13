#pragma once

#include "core/lux.h"
#include "core/idevice.h"

namespace Lux
{
	namespace FS
	{
		class IFile;

		class LUX_CORE_API MemoryFileSystem : public IFileDevice
		{
		public:
			virtual IFile* create(IFile* child) LUX_OVERRIDE;

			const char* name() const { return "memory"; }
		};
	} // ~namespace FS
} // ~namespace Lux
#pragma once

#include "core/lux.h"
#include "core/idevice.h"

namespace Lux
{
	namespace FS
	{
		class IFile;

		class LUX_CORE_API DiskFileSystem : public IFileDevice
		{
		public:
			virtual IFile* create(IFile* child) LUX_OVERRIDE;
			
			const char* name() const { return "disk"; }
		};
	} // ~namespace FS
} // ~namespace Lux
#pragma once

#include "core/lux.h"
#include "core/fs/ifile_device.h"

namespace Lux
{
	namespace FS
	{
		class IFile;

		class LUX_CORE_API DiskFileDevice : public IFileDevice
		{
		public:
			virtual IFile* createFile(IFile* child) override;
			
			const char* name() const { return "disk"; }
		};
	} // ~namespace FS
} // ~namespace Lux
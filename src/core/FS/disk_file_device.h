#pragma once

#include "core/lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	namespace FS
	{
		class IFile;

		class LUMIX_CORE_API DiskFileDevice : public IFileDevice
		{
		public:
			virtual IFile* createFile(IFile* child) override;
			
			const char* name() const { return "disk"; }
		};
	} // ~namespace FS
} // ~namespace Lumix

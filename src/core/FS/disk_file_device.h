#pragma once

#include "core/lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;

		class LUMIX_CORE_API DiskFileDevice : public IFileDevice
		{
		public:
			DiskFileDevice(IAllocator& allocator) : m_allocator(allocator) {}

			virtual IFile* createFile(IFile* child) override;
			virtual void destroyFile(IFile* file) override;

			const char* name() const override { return "disk"; }
		
		private:
			IAllocator& m_allocator;
		};
	} // ~namespace FS
} // ~namespace Lumix

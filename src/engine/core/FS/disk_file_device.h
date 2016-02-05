#pragma once

#include "lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;

		class LUMIX_ENGINE_API DiskFileDevice : public IFileDevice
		{
		public:
			explicit DiskFileDevice(IAllocator& allocator) : m_allocator(allocator) {}

			IFile* createFile(IFile* child) override;
			void destroyFile(IFile* file) override;

			const char* name() const override { return "disk"; }
		
		private:
			IAllocator& m_allocator;
		};
	} // ~namespace FS
} // ~namespace Lumix

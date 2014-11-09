#pragma once

#include "core/lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;

		class LUMIX_CORE_API MemoryFileDevice : public IFileDevice
		{
		public:
			MemoryFileDevice(IAllocator& allocator) : m_allocator(allocator) {}

			virtual void destroyFile(IFile* file) override;
			virtual IFile* createFile(IFile* child) override;

			const char* name() const { return "memory"; }

		private:
			IAllocator& m_allocator;
		};
	} // ~namespace FS
} // ~namespace Lumix

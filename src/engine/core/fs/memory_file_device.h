#pragma once

#include "lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;

		class LUMIX_ENGINE_API MemoryFileDevice : public IFileDevice
		{
		public:
			explicit MemoryFileDevice(IAllocator& allocator) : m_allocator(allocator) {}

			void destroyFile(IFile* file) override;
			IFile* createFile(IFile* child) override;

			const char* name() const override { return "memory"; }

		private:
			IAllocator& m_allocator;
		};
	} // ~namespace FS
} // ~namespace Lumix

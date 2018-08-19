#pragma once

#include "engine/fs/ifile_device.h"
#include "engine/lumix.h"

namespace Lumix
{
	struct IAllocator;

	namespace FS
	{
		struct IFile;

		class LUMIX_ENGINE_API MemoryFileDevice final : public IFileDevice
		{
		public:
			explicit MemoryFileDevice(IAllocator& allocator) : m_allocator(allocator) {}

			void destroyFile(IFile* file) override;
			IFile* createFile(IFile* child) override;

			const char* name() const override { return "memory"; }

		private:
			IAllocator& m_allocator;
		};
	} // namespace FS
} // namespace Lumix

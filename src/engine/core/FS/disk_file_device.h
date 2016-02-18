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
			DiskFileDevice(const char* base_path0, const char* base_path1, IAllocator& allocator);

			IFile* createFile(IFile* child) override;
			void destroyFile(IFile* file) override;
			const char* getBasePath(int index) const { return m_base_paths[index]; }
			const char* name() const override { return "disk"; }
		
		private:
			IAllocator& m_allocator;
			char m_base_paths[2][MAX_PATH_LENGTH];
		};
	} // ~namespace FS
} // ~namespace Lumix

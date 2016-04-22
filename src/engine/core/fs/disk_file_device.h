#pragma once

#include "lumix.h"
#include "engine/core/fs/ifile_device.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;

		class LUMIX_ENGINE_API DiskFileDevice : public IFileDevice
		{
		public:
			DiskFileDevice(const char* name, const char* base_path, IAllocator& allocator);

			IFile* createFile(IFile* child) override;
			void destroyFile(IFile* file) override;
			const char* getBasePath() const { return m_base_path; }
			const char* name() const override { return m_name; }
		
		private:
			IAllocator& m_allocator;
			char m_base_path[MAX_PATH_LENGTH];
			char m_name[20];
		};
	} // ~namespace FS
} // ~namespace Lumix

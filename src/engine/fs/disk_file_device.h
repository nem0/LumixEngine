#pragma once

#include "engine/fs/ifile_device.h"
#include "engine/lumix.h"

namespace Lumix
{
	struct IAllocator;

	namespace FS
	{
		struct IFile;

		class LUMIX_ENGINE_API DiskFileDevice LUMIX_FINAL : public IFileDevice
		{
		public:
			DiskFileDevice(const char* name, const char* base_path, IAllocator& allocator);

			IFile* createFile(IFile* child) override;
			void destroyFile(IFile* file) override;
			const char* getBasePath() const { return m_base_path; }
			void setBasePath(const char* path);
			const char* name() const override { return m_name; }
		
		private:
			IAllocator& m_allocator;
			char m_base_path[MAX_PATH_LENGTH];
			char m_name[20];
		};
	} // namespace FS
} // namespace Lumix

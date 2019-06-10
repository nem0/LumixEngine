#pragma once

#include "engine/fs/ifile_device.h"
#include "engine/lumix.h"

namespace Lumix
{
struct IAllocator;

namespace FS
{
	struct IFile;

	class LUMIX_ENGINE_API DiskFileDevice final : public IFileDevice
	{
	public:
		DiskFileDevice(const char* base_path, IAllocator& allocator);

		IFile* createFile() override;
		void destroyFile(IFile* file) override;
		const char* getBasePath() const { return m_base_path; }
		void setBasePath(const char* path);
		
	private:
		IAllocator& m_allocator;
		char m_base_path[MAX_PATH_LENGTH];
	};
} // namespace FS
} // namespace Lumix

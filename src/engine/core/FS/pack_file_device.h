#pragma once#pragma once

#include "core/fs/ifile_device.h"
#include "core/fs/os_file.h"
#include "core/hash_map.h"
#include "lumix.h"

namespace Lumix
{
class IAllocator;

namespace FS
{
class IFile;


class LUMIX_ENGINE_API PackFileDevice : public IFileDevice
{
	friend class PackFile;
public:
	PackFileDevice(IAllocator& allocator);
	~PackFileDevice();

	IFile* createFile(IFile* child) override;
	void destroyFile(IFile* file) override;
	const char* name() const override { return "pack"; }
	bool mount(const char* path);

private:
	struct PackFileInfo
	{
		size_t offset;
		size_t size;
	};

	HashMap<uint32, PackFileInfo> m_files;
	size_t m_offset;
	OsFile m_file;
	IAllocator& m_allocator;
};


} // namespace FS
} // namespace Lumix

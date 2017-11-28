#pragma once

#include "engine/fs/ifile_device.h"
#include "engine/fs/os_file.h"
#include "engine/hash_map.h"
#include "engine/lumix.h"


namespace Lumix
{
struct IAllocator;

namespace FS
{
struct IFile;


class LUMIX_ENGINE_API PackFileDevice LUMIX_FINAL : public IFileDevice
{
	friend class PackFile;
public:
	explicit PackFileDevice(IAllocator& allocator);
	~PackFileDevice();

	IFile* createFile(IFile* child) override;
	void destroyFile(IFile* file) override;
	const char* name() const override { return "pack"; }
	bool mount(const char* path);

private:
	struct PackFileInfo
	{
		u64 offset;
		u64 size;
	};

	HashMap<u32, PackFileInfo> m_files;
	size_t m_offset;
	OsFile m_file;
	IAllocator& m_allocator;
};


} // namespace FS
} // namespace Lumix

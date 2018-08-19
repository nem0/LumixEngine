#pragma once

#include "engine/fs/ifile_device.h"
#include "engine/lumix.h"

struct mf_resource;

namespace Lumix
{
struct IAllocator;

namespace FS
{
struct IFile;

class LUMIX_ENGINE_API ResourceFileDevice final : public IFileDevice
{
public:
	explicit ResourceFileDevice(IAllocator& allocator)
		: m_allocator(allocator)
	{
	}

	void destroyFile(IFile* file) override;
	IFile* createFile(IFile* child) override;
	int getResourceFilesCount() const;
	const mf_resource* getResource(int index) const;

	const char* name() const override { return "resource"; }

private:
	IAllocator& m_allocator;
};

} // namespace FS

} // namespace Lumix

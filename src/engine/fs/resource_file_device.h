#pragma once

#include "engine/lumix.h"
#include "engine/fs/ifile_device.h"

struct mf_resource;

namespace Lumix
{
class IAllocator;

namespace FS
{
class IFile;

class LUMIX_ENGINE_API ResourceFileDevice LUMIX_FINAL : public IFileDevice
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

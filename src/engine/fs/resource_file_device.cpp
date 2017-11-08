#include "engine/fs/resource_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/iallocator.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/string.h"
#include "stb/mf_resource.h"


namespace Lumix
{
namespace FS
{


class LUMIX_ENGINE_API ResourceFile LUMIX_FINAL : public IFile
{
public:
	ResourceFile(IFile* file, ResourceFileDevice& device, IAllocator& allocator)
		: m_device(device)
		, m_allocator(allocator)
		, m_resource(nullptr)
	{
		ASSERT(file == nullptr);
	}

	~ResourceFile() = default;


	IFileDevice& getDevice() override { return m_device; }


	bool open(const Path& path, Mode mode) override
	{
		ASSERT(!m_resource); // reopen is not supported currently
		ASSERT((mode & Mode::WRITE) == 0);
		if ((mode & Mode::WRITE) != 0) return false;
		int count = mf_get_all_resources_count();
		const mf_resource* resources = mf_get_all_resources();
		m_resource = nullptr;
		for (int i = 0; i < count; ++i)
		{
			const mf_resource* res = &resources[i];
			if (equalIStrings(path.c_str(), res->path))
			{
				m_resource = res;
				break;
			}
		}
		if (!m_resource) return false;
		m_pos = 0;
		return true;
	}

	void close() override
	{
		m_resource = nullptr;
	}

	bool read(void* buffer, size_t size) override
	{
		size_t amount = m_pos + size < m_resource->size ? size : m_resource->size - m_pos;
		copyMemory(buffer, m_resource->value + m_pos, (int)amount);
		m_pos += amount;
		return amount == size;
	}

	bool write(const void* buffer, size_t size) override
	{
		ASSERT(false);
		return false;
	}

	const void* getBuffer() const override { return m_resource->value; }

	size_t size() override { return m_resource->size; }

	bool seek(SeekMode base, size_t pos) override
	{
		switch (base)
		{
			case SeekMode::BEGIN:
				ASSERT(pos <= (i32)m_resource->size);
				m_pos = pos;
				break;
			case SeekMode::CURRENT:
				ASSERT(0 <= i32(m_pos + pos) && i32(m_pos + pos) <= i32(m_resource->size));
				m_pos += pos;
				break;
			case SeekMode::END:
				ASSERT(pos <= (i32)m_resource->size);
				m_pos = m_resource->size - pos;
				break;
			default: ASSERT(0); break;
		}

		bool ret = m_pos <= m_resource->size;
		m_pos = Math::minimum(m_pos, m_resource->size);
		return ret;
	}

	size_t pos() override { return m_pos; }

private:
	IAllocator& m_allocator;
	ResourceFileDevice& m_device;
	const mf_resource* m_resource;
	size_t m_pos;
};


void ResourceFileDevice::destroyFile(IFile* file)
{
	LUMIX_DELETE(m_allocator, file);
}


const mf_resource* ResourceFileDevice::getResource(int index) const
{
	return &mf_get_all_resources()[index];
}


int ResourceFileDevice::getResourceFilesCount() const
{
	return mf_get_all_resources_count();
}


IFile* ResourceFileDevice::createFile(IFile* child)
{
	return LUMIX_NEW(m_allocator, ResourceFile)(child, *this, m_allocator);
}

} // namespace FS
} // namespace Lumix

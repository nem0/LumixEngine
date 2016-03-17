#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/iallocator.h"
#include "core/path.h"
#include "core/string.h"
#include "pack_file_device.h"


namespace Lumix
{
namespace FS
{


class PackFile : public IFile
{
public:
	PackFile(PackFileDevice& device, IAllocator& allocator)
		: m_device(device)
		, m_allocator(allocator)
		, m_local_offset(0)
	{
	}


	bool open(const Path& path, Mode mode) override
	{
		auto iter = m_device.m_files.find(path.getHash());
		if (iter == m_device.m_files.end()) return false;
		m_file = iter.value();
		m_local_offset = 0;
		return m_device.m_file.seek(SeekMode::BEGIN, (size_t)m_file.offset) == (size_t)m_file.offset;
	}


	bool read(void* buffer, size_t size) override
	{
		if (m_device.m_offset != m_file.offset + m_local_offset)
		{
			if (m_device.m_file.seek(FS::SeekMode::BEGIN, size_t(m_file.offset + m_local_offset)) !=
				size_t(m_file.offset + m_local_offset))
			{
				return false;
			}
		}
		m_local_offset += size;
		return m_device.m_file.read(buffer, size);
	}


	size_t seek(SeekMode base, size_t pos) override
	{
		m_local_offset = pos;
		return m_device.m_file.seek(SeekMode::BEGIN, size_t(m_file.offset + pos));
	}


	IFileDevice& getDevice() override { return m_device; }
	void close() override { m_local_offset = 0; }
	bool write(const void* buffer, size_t size) override { ASSERT(false); return false; }
	const void* getBuffer() const override { return nullptr; }
	size_t size() override { return (size_t)m_file.size; }
	size_t pos() override { return m_local_offset; }

private:
	virtual ~PackFile() {}

	PackFileDevice::PackFileInfo m_file;
	PackFileDevice& m_device;
	size_t m_local_offset;
	IAllocator& m_allocator;
}; // class PackFile


PackFileDevice::PackFileDevice(IAllocator& allocator)
	: m_allocator(allocator)
	, m_files(allocator)
{
}


PackFileDevice::~PackFileDevice()
{
	m_file.close();
}


bool PackFileDevice::mount(const char* path)
{
	m_file.close();
	if(!m_file.open(path, Mode::OPEN_AND_READ, m_allocator)) return false;

	int32 count;
	m_file.read(&count, sizeof(count));
	for(int i = 0; i < count; ++i)
	{
		uint32 hash;
		m_file.read(&hash, sizeof(hash));
		PackFileInfo info;
		m_file.read(&info, sizeof(info));
		m_files.insert(hash, info);
	}
	m_offset = m_file.pos();
	return true;
}


void PackFileDevice::destroyFile(IFile* file)
{
	LUMIX_DELETE(m_allocator, file);
}


IFile* PackFileDevice::createFile(IFile*)
{
	return LUMIX_NEW(m_allocator, PackFile)(*this, m_allocator);
}


} // namespace FS
} // namespace Lumix

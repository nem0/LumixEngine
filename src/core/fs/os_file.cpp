#include "core/fs/os_file.h"
#include "core/iallocator.h"
#include "core/lumix.h"

#include <assert.h>
#include <windows.h>

namespace Lumix
{
namespace FS
{
struct OsFileImpl
{
	OsFileImpl(IAllocator& allocator)
		: m_allocator(allocator)
	{
	}

	IAllocator& m_allocator;
	HANDLE m_file;
};

OsFile::OsFile()
{
	m_impl = nullptr;
}

OsFile::~OsFile()
{
	ASSERT(!m_impl);
}

bool OsFile::open(const char* path, Mode mode, IAllocator& allocator)
{
	HANDLE hnd = ::CreateFile(
		path,
		Mode::WRITE & mode ? GENERIC_WRITE
						   : 0 | Mode::READ & mode ? GENERIC_READ : 0,
		Mode::WRITE & mode ? 0 : FILE_SHARE_READ,
		nullptr,
		Mode::OPEN_OR_CREATE & mode
			? OPEN_ALWAYS
			: (Mode::CREATE & mode ? CREATE_ALWAYS : OPEN_EXISTING),
		FILE_ATTRIBUTE_NORMAL,
		nullptr);


	if (INVALID_HANDLE_VALUE != hnd)
	{
		OsFileImpl* impl = allocator.newObject<OsFileImpl>(allocator);
		impl->m_file = hnd;
		m_impl = impl;

		return true;
	}

	return false;
}

void OsFile::close()
{
	if (nullptr != m_impl)
	{
		::CloseHandle(m_impl->m_file);
		m_impl->m_allocator.deleteObject(m_impl);
		m_impl = nullptr;
	}
}

bool OsFile::write(const void* data, size_t size)
{
	ASSERT(nullptr != m_impl);
	size_t written = 0;
	::WriteFile(m_impl->m_file, data, (DWORD)size, (LPDWORD)&written, nullptr);
	return size == written;
}

bool OsFile::read(void* data, size_t size)
{
	ASSERT(nullptr != m_impl);
	size_t readed = 0;
	::ReadFile(m_impl->m_file, data, (DWORD)size, (LPDWORD)&readed, nullptr);
	return size == readed;
}

size_t OsFile::size()
{
	ASSERT(nullptr != m_impl);
	return ::GetFileSize(m_impl->m_file, 0);
}

size_t OsFile::pos()
{
	ASSERT(nullptr != m_impl);
	return ::SetFilePointer(m_impl->m_file, 0, nullptr, FILE_CURRENT);
}

size_t OsFile::seek(SeekMode base, size_t pos)
{
	ASSERT(nullptr != m_impl);
	int dir = 0;
	switch (base)
	{
		case SeekMode::BEGIN:
			dir = FILE_BEGIN;
			break;
		case SeekMode::END:
			dir = FILE_END;
			break;
		case SeekMode::CURRENT:
			dir = FILE_CURRENT;
			break;
	}

	return ::SetFilePointer(m_impl->m_file, (DWORD)pos, nullptr, dir);
}

void OsFile::writeEOF()
{
	ASSERT(nullptr != m_impl);
	::SetEndOfFile(m_impl->m_file);
}
} // ~namespace FS
} // ~namespace Lumix

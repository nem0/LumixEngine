#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/win/simple_win.h"
#include "engine/string.h"
#include "engine/lumix.h"


namespace Lumix
{
namespace FS
{
struct OsFileImpl
{
	explicit OsFileImpl(IAllocator& allocator)
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
	HANDLE hnd = ::CreateFile(path,
		Mode::WRITE & mode ? GENERIC_WRITE : 0 | Mode::READ & mode ? GENERIC_READ : 0,
		Mode::WRITE & mode ? 0 : FILE_SHARE_READ,
		nullptr,
		Mode::CREATE & mode ? CREATE_ALWAYS : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);


	if (INVALID_HANDLE_VALUE != hnd)
	{
		OsFileImpl* impl = LUMIX_NEW(allocator, OsFileImpl)(allocator);
		impl->m_file = hnd;
		m_impl = impl;

		return true;
	}

	return false;
}

void OsFile::flush()
{
	ASSERT(nullptr != m_impl);
	FlushFileBuffers(m_impl->m_file);
}

void OsFile::close()
{
	if (nullptr != m_impl)
	{
		::CloseHandle(m_impl->m_file);
		LUMIX_DELETE(m_impl->m_allocator, m_impl);
		m_impl = nullptr;
	}
}

bool OsFile::writeText(const char* text)
{
	int len = stringLength(text);
	return write(text, len);
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

bool OsFile::fileExists(const char* path)
{
	DWORD dwAttrib = GetFileAttributes(path);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

size_t OsFile::pos()
{
	ASSERT(nullptr != m_impl);
	return ::SetFilePointer(m_impl->m_file, 0, nullptr, FILE_CURRENT);
}

bool OsFile::seek(SeekMode base, size_t pos)
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

	return ::SetFilePointer(m_impl->m_file, (LONG)pos, nullptr, dir) == (LONG)pos;
}


OsFile& OsFile::operator <<(const char* text)
{
	write(text, stringLength(text));
	return *this;
}


OsFile& OsFile::operator <<(i32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OsFile& OsFile::operator <<(u32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OsFile& OsFile::operator <<(u64 value)
{
	char buf[30];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OsFile& OsFile::operator <<(float value)
{
	char buf[128];
	toCString(value, buf, lengthOf(buf), 7);
	write(buf, stringLength(buf));
	return *this;
}


} // namespace FS
} // namespace Lumix

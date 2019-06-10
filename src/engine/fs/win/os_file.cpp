#include "engine/fs/os_file.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/string.h"
#include "engine/win/simple_win.h"


namespace Lumix
{
namespace FS
{


OsFile::OsFile()
{
	m_handle = (void*)INVALID_HANDLE_VALUE;
	static_assert(sizeof(m_handle) >= sizeof(HANDLE), "");
}

OsFile::~OsFile()
{
	ASSERT((HANDLE)m_handle == INVALID_HANDLE_VALUE);
}

bool OsFile::open(const char* path, Mode mode)
{
	m_handle = (HANDLE)::CreateFile(path,
		(Mode::WRITE & mode) ? GENERIC_WRITE : 0 | ((Mode::READ & mode) ? GENERIC_READ : 0),
		(Mode::WRITE & mode) ? 0 : FILE_SHARE_READ,
		nullptr,
		(Mode::CREATE & mode) ? CREATE_ALWAYS : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);


	return INVALID_HANDLE_VALUE != m_handle;
}

void OsFile::flush()
{
	ASSERT(nullptr != m_handle);
	FlushFileBuffers((HANDLE)m_handle);
}

void OsFile::close()
{
	if (INVALID_HANDLE_VALUE != (HANDLE)m_handle)
	{
		::CloseHandle((HANDLE)m_handle);
		m_handle = (void*)INVALID_HANDLE_VALUE;
	}
}

bool OsFile::writeText(const char* text)
{
	int len = stringLength(text);
	return write(text, len);
}

bool OsFile::write(const void* data, size_t size)
{
	ASSERT(INVALID_HANDLE_VALUE != (HANDLE)m_handle);
	size_t written = 0;
	::WriteFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&written, nullptr);
	return size == written;
}

bool OsFile::read(void* data, size_t size)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	DWORD readed = 0;
	BOOL success = ::ReadFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&readed, nullptr);
	return success && size == readed;
}

size_t OsFile::size()
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::GetFileSize((HANDLE)m_handle, 0);
}

bool OsFile::fileExists(const char* path)
{
	DWORD dwAttrib = GetFileAttributes(path);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

size_t OsFile::pos()
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::SetFilePointer((HANDLE)m_handle, 0, nullptr, FILE_CURRENT);
}

bool OsFile::seek(SeekMode base, size_t pos)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
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
		default:
			ASSERT(false);
			break;
	}

	LARGE_INTEGER dist;
	dist.QuadPart = pos;
	return ::SetFilePointer((HANDLE)m_handle, dist.u.LowPart, &dist.u.HighPart, dir) != INVALID_SET_FILE_POINTER;
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


bool OSFileStream::open(const Path& path, FS::Mode mode)
{
	return file.open(path.c_str(), mode);
}


void OSFileStream::close()
{
	file.close();
}


bool OSFileStream::read(void* buffer, size_t size)
{
	return file.read(buffer, size);
}


bool OSFileStream::write(const void* buffer, size_t size)
{
	return file.write(buffer, size);
}


const void* OSFileStream::getBuffer() const
{
	return nullptr;
}


size_t OSFileStream::size()
{
	return file.size();
}


bool OSFileStream::seek(FS::SeekMode base, size_t pos)
{
	return file.seek(base, pos);
}


size_t OSFileStream::pos()
{
	return file.pos();
}


FS::IFileDevice* OSFileStream::getDevice()
{
	return nullptr;
}


} // namespace FS
} // namespace Lumix

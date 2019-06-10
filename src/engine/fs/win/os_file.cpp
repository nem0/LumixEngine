#include "engine/fs/os_file.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/string.h"
#include "engine/win/simple_win.h"


namespace Lumix
{
namespace FS
{


OSInputFile::OSInputFile()
{
	m_handle = (void*)INVALID_HANDLE_VALUE;
	static_assert(sizeof(m_handle) >= sizeof(HANDLE), "");
}


OSOutputFile::OSOutputFile()
{
	m_handle = (void*)INVALID_HANDLE_VALUE;
	static_assert(sizeof(m_handle) >= sizeof(HANDLE), "");
}


OSInputFile::~OSInputFile()
{
	ASSERT((HANDLE)m_handle == INVALID_HANDLE_VALUE);
}


OSOutputFile::~OSOutputFile()
{
	ASSERT((HANDLE)m_handle == INVALID_HANDLE_VALUE);
}


bool OSOutputFile::open(const char* path)
{
	m_handle = (HANDLE)::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	return INVALID_HANDLE_VALUE != m_handle;
}


bool OSInputFile::open(const char* path)
{
	m_handle = (HANDLE)::CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	return INVALID_HANDLE_VALUE != m_handle;
}


void OSOutputFile::flush()
{
	ASSERT(nullptr != m_handle);
	FlushFileBuffers((HANDLE)m_handle);
}


void OSOutputFile::close()
{
	if (INVALID_HANDLE_VALUE != (HANDLE)m_handle)
	{
		::CloseHandle((HANDLE)m_handle);
		m_handle = (void*)INVALID_HANDLE_VALUE;
	}
}


void OSInputFile::close()
{
	if (INVALID_HANDLE_VALUE != (HANDLE)m_handle)
	{
		::CloseHandle((HANDLE)m_handle);
		m_handle = (void*)INVALID_HANDLE_VALUE;
	}
}


bool OSOutputFile::write(const void* data, u64 size)
{
	ASSERT(INVALID_HANDLE_VALUE != (HANDLE)m_handle);
	u64 written = 0;
	::WriteFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&written, nullptr);
	return size == written;
}

bool OSInputFile::read(void* data, u64 size)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	DWORD readed = 0;
	BOOL success = ::ReadFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&readed, nullptr);
	return success && size == readed;
}

u64 OSInputFile::size() const
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::GetFileSize((HANDLE)m_handle, 0);
}


u64 OSOutputFile::pos()
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::SetFilePointer((HANDLE)m_handle, 0, nullptr, FILE_CURRENT);
}


u64 OSInputFile::pos()
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::SetFilePointer((HANDLE)m_handle, 0, nullptr, FILE_CURRENT);
}


bool OSInputFile::seek(u64 pos)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	LARGE_INTEGER dist;
	dist.QuadPart = pos;
	return ::SetFilePointer((HANDLE)m_handle, dist.u.LowPart, &dist.u.HighPart, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}


OSOutputFile& OSOutputFile::operator <<(const char* text)
{
	write(text, stringLength(text));
	return *this;
}


OSOutputFile& OSOutputFile::operator <<(i32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OSOutputFile& OSOutputFile::operator <<(u32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OSOutputFile& OSOutputFile::operator <<(u64 value)
{
	char buf[30];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OSOutputFile& OSOutputFile::operator <<(float value)
{
	char buf[128];
	toCString(value, buf, lengthOf(buf), 7);
	write(buf, stringLength(buf));
	return *this;
}


} // namespace FS
} // namespace Lumix

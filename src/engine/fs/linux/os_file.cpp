#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/string.h"
#include "engine/lumix.h"
#include <cstdio>
#include <unistd.h>


namespace Lumix
{
namespace FS
{


OsFile::OsFile()
{
	m_handle = nullptr;
}

OsFile::~OsFile()
{
	ASSERT(!m_handle);
}

bool OsFile::open(const char* path, Mode mode, IAllocator& allocator)
{
	m_handle = fopen(path, Mode::WRITE & mode ? "wb" : "rb");
	return m_handle;
}

void OsFile::flush()
{
	ASSERT(nullptr != m_handle);
	fflush(m_handle);
}

void OsFile::close()
{
	if (nullptr != m_handle)
	{
		fclose(m_handle);
		m_handle = nullptr;
	}
}

bool OsFile::writeText(const char* text)
{
	int len = stringLength(text);
	return write(text, len);
}

bool OsFile::write(const void* data, size_t size)
{
	ASSERT(nullptr != m_handle);
	size_t written = fwrite(data, size, 1, m_handle);
	return written == 1;
}

bool OsFile::read(void* data, size_t size)
{
	ASSERT(nullptr != m_handle);
	size_t read = fread(data, size, 1, m_handle);
	return read == 1;
}

size_t OsFile::size()
{
	ASSERT(nullptr != m_handle);
	long pos = ftell(m_handle);
	fseek(m_handle, 0, SEEK_END);
	size_t size = (size_t)ftell(m_handle);
	fseek(m_handle, pos, SEEK_SET);
	return size;
}

bool OsFile::fileExists(const char* path)
{
	return access(path, F_OK) != -1;
}

size_t OsFile::pos()
{
	ASSERT(nullptr != m_handle);
	long pos = ftell(m_handle);
	return (size_t)pos;
}

bool OsFile::seek(SeekMode base, size_t pos)
{
	ASSERT(nullptr != m_handle);
	int dir = 0;
	switch (base)
	{
		case SeekMode::BEGIN:
			dir = SEEK_SET;
			break;
		case SeekMode::END:
			dir = SEEK_END;
			break;
		case SeekMode::CURRENT:
			dir = SEEK_CUR;
			break;
	}

	return fseek(m_handle, pos, dir) == 0;
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
	char buf[30];
	toCString(value, buf, lengthOf(buf), 1);
	write(buf, stringLength(buf));
	return *this;
}


} // namespace FS
} // namespace Lumix

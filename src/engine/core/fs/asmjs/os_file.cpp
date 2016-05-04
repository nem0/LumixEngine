#include "engine/core/fs/os_file.h"
#include "engine/core/iallocator.h"
#include "engine/core/string.h"
#include "engine/lumix.h"
#include <cstdio>
#include <unistd.h>


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
	FILE* m_file;
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
	FILE* fp = fopen(path, Mode::WRITE & mode ? "wb" : "rb");
	if (fp)
	{
		OsFileImpl* impl = LUMIX_NEW(allocator, OsFileImpl)(allocator);
		impl->m_file = fp;
		m_impl = impl;

		return true;
	}
	return false;
}

void OsFile::flush()
{
	ASSERT(nullptr != m_impl);
	fflush(m_impl->m_file);
}

void OsFile::close()
{
	if (nullptr != m_impl)
	{
		fclose(m_impl->m_file);
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
	size_t written = fwrite(data, size, 1, m_impl->m_file);
	return written == 1;
}

bool OsFile::read(void* data, size_t size)
{
	ASSERT(nullptr != m_impl);
	size_t read = fread(data, size, 1, m_impl->m_file);
	return read == 1;
}

size_t OsFile::size()
{
	ASSERT(nullptr != m_impl);
	long pos = ftell(m_impl->m_file);
	fseek(m_impl->m_file, 0, SEEK_END);
	size_t size = (size_t)ftell(m_impl->m_file);
	fseek(m_impl->m_file, pos, SEEK_SET);
	return size;
}

bool OsFile::fileExists(const char* path)
{
	return access(path, F_OK) != -1;
}

size_t OsFile::pos()
{
	ASSERT(nullptr != m_impl);
	long pos = ftell(m_impl->m_file);
	return (size_t)pos;
}

bool OsFile::seek(SeekMode base, size_t pos)
{
	ASSERT(nullptr != m_impl);
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

	return fseek(m_impl->m_file, pos, dir) == 0;
}


OsFile& OsFile::operator <<(const char* text)
{
	write(text, stringLength(text));
	return *this;
}


OsFile& OsFile::operator <<(int32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OsFile& OsFile::operator <<(uint32 value)
{
	char buf[20];
	toCString(value, buf, lengthOf(buf));
	write(buf, stringLength(buf));
	return *this;
}


OsFile& OsFile::operator <<(uint64 value)
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

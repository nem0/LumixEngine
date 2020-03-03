#include "stream.h"
#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/string.h"


namespace Lumix
{

OutputMemoryStream::OutputMemoryStream(IAllocator& allocator)
	: m_allocator(&allocator)
	, m_data(nullptr)
	, m_size(0)
	, m_pos(0)
{}


OutputMemoryStream::OutputMemoryStream(void* data, u64 size)
	: m_data((u8*)data)
	, m_size(size)
	, m_allocator(nullptr)
	, m_pos(0)
{
}


OutputMemoryStream::OutputMemoryStream(const OutputMemoryStream& blob, IAllocator& allocator)
	: m_allocator(&allocator)
	, m_pos(blob.m_pos)
{
	if (blob.m_size > 0)
	{
		m_data = (u8*)allocator.allocate(blob.m_size);
		memcpy(m_data, blob.m_data, blob.m_size);
		m_size = blob.m_size;
	}
	else
	{
		m_data = nullptr;
		m_size = 0;
	}
}


OutputMemoryStream::OutputMemoryStream(const InputMemoryStream& blob, IAllocator& allocator)
	: m_allocator(&allocator)
	, m_pos(blob.size())
{
	if (blob.size() > 0)
	{
		m_data = (u8*)allocator.allocate(blob.size());
		memcpy(m_data, blob.getData(), blob.size());
		m_size = blob.size();
	}
	else
	{
		m_data = nullptr;
		m_size = 0;
	}
}


OutputMemoryStream::~OutputMemoryStream()
{
	if (m_allocator) m_allocator->deallocate(m_data);
}


IOutputStream& IOutputStream::operator << (const char* str)
{
	write(str, stringLength(str));
	return *this;
}


IOutputStream& IOutputStream::operator << (i32 value)
{
	char tmp[20];
	toCString(value, Span(tmp));
	write(tmp, stringLength(tmp));
	return *this;
}


IOutputStream& IOutputStream::operator << (u64 value)
{
	char tmp[40];
	toCString(value, Span(tmp));
	write(tmp, stringLength(tmp));
	return *this;
}


IOutputStream& IOutputStream::operator << (i64 value)
{
	char tmp[40];
	toCString(value, Span(tmp));
	write(tmp, stringLength(tmp));
	return *this;
}


IOutputStream& IOutputStream::operator << (u32 value)
{
	char tmp[20];
	toCString(value, Span(tmp));
	write(tmp, stringLength(tmp));
	return *this;
}


IOutputStream& IOutputStream::operator << (float value)
{
	char tmp[30];
	toCString(value, Span(tmp), 6);
	write(tmp, stringLength(tmp));
	return *this;
}


IOutputStream& IOutputStream::operator << (double value)
{
	char tmp[40];
	toCString(value, Span(tmp), 12);
	write(tmp, stringLength(tmp));
	return *this;
}


OutputMemoryStream::OutputMemoryStream(OutputMemoryStream&& rhs)
{
	m_allocator = rhs.m_allocator;
	m_pos = rhs.m_pos;
	m_size = rhs.m_size;
	m_data = rhs.m_data;
	
	rhs.m_data = nullptr;
	rhs.m_size = 0;
	rhs.m_pos = 0;
}



OutputMemoryStream::OutputMemoryStream(const OutputMemoryStream& rhs)
{
	m_allocator = rhs.m_allocator;
	m_pos = rhs.m_pos;
	if (rhs.m_size > 0)
	{
		m_data = (u8*)m_allocator->allocate(rhs.m_size);
		memcpy(m_data, rhs.m_data, rhs.m_size);
		m_size = rhs.m_size;
	}
	else
	{
		m_data = nullptr;
		m_size = 0;
	}
}


void OutputMemoryStream::operator =(const OutputMemoryStream& rhs)
{
	ASSERT(rhs.m_allocator);
	if (m_allocator) m_allocator->deallocate(m_data);
		
	m_allocator = rhs.m_allocator;
	m_pos = rhs.m_pos;
	if (rhs.m_size > 0)
	{
		m_data = (u8*)m_allocator->allocate(rhs.m_size);
		memcpy(m_data, rhs.m_data, rhs.m_size);
		m_size = rhs.m_size;
	}
	else
	{
		m_data = nullptr;
		m_size = 0;
	}
}


void OutputMemoryStream::operator =(OutputMemoryStream&& rhs)
{
	ASSERT(rhs.m_allocator);
	if (m_allocator) m_allocator->deallocate(m_data);
		
	m_allocator = rhs.m_allocator;
	m_pos = rhs.m_pos;
	m_data = rhs.m_data;
	m_size = rhs.m_size;

	rhs.m_pos = 0;
	rhs.m_size = 0;
	rhs.m_data = nullptr;
}
	

void OutputMemoryStream::write(const String& string)
{
	writeString(string.c_str());
}


void* OutputMemoryStream::skip(int size)
{
	ASSERT(size > 0);

	if (m_pos + size > m_size)
	{
		reserve((m_pos + size) << 1);
	}
	void* ret = (u8*)m_data + m_pos;
	m_pos += size;
	return ret;
}


bool OutputMemoryStream::write(const void* data, u64 size)
{
	if (!size) return true;

	if (m_pos + size > m_size)
	{
		reserve((m_pos + size) << 1);
	}
	memcpy((u8*)m_data + m_pos, data, size);
	m_pos += size;
	return true;
}


void OutputMemoryStream::writeString(const char* string)
{
	if (string) {
		const i32 size = stringLength(string) + 1;
		write(string, size);
	} else {
		write((char)0);
	}
}


void OutputMemoryStream::clear()
{
	m_pos = 0;
}


void OutputMemoryStream::reserve(u64 size)
{
	if (size <= m_size) return;

	ASSERT(m_allocator);
	u8* tmp = (u8*)m_allocator->allocate(size);
	memcpy(tmp, m_data, m_size);
	m_allocator->deallocate(m_data);
	m_data = tmp;
	m_size = size;
}


Span<u8> OutputMemoryStream::releaseOwnership() {
	Span<u8> res((u8*)m_data, (u8*)m_data + m_size);
	m_data = nullptr;
	m_pos = m_size = 0;
	return res;
}


void OutputMemoryStream::resize(u64 size)
{
	m_pos = size;
	if (size <= m_size) return;

	ASSERT(m_allocator);
	u8* tmp = (u8*)m_allocator->allocate(size);
	memcpy(tmp, m_data, m_size);
	m_allocator->deallocate(m_data);
	m_data = tmp;
	m_size = size;
}



InputMemoryStream::InputMemoryStream(const void* data, u64 size)
	: m_data((const u8*)data)
	, m_size(size)
	, m_pos(0)
{}


InputMemoryStream::InputMemoryStream(const OutputMemoryStream& blob)
	: m_data((const u8*)blob.getData())
	, m_size(blob.getPos())
	, m_pos(0)
{}


void InputMemoryStream::set(const void* data, u64 size) {
	m_data = (u8*)data;
	m_size = size;
	m_pos = 0;
}


const void* InputMemoryStream::skip(u64 size)
{
	auto* pos = m_data + m_pos;
	m_pos += size;
	if (m_pos > m_size)
	{
		m_pos = m_size;
	}

	return (const void*)pos;
}


bool InputMemoryStream::read(void* data, u64 size)
{
	if (m_pos + (int)size > m_size)
	{
		for (i32 i = 0; i < size; ++i)
			((unsigned char*)data)[i] = 0;
		return false;
	}
	if (size)
	{
		memcpy(data, ((char*)m_data) + m_pos, size);
	}
	m_pos += size;
	return true;
}


bool InputMemoryStream::read(String& string)
{
	u32 size;
	IInputStream::read(size);
	string.resize(size);
	return read(string.getData(), size);
}


const char* InputMemoryStream::readString()
{
	char c;
	const char* ret = (const char*)m_data + m_pos;
	do {
		c = IInputStream::read<char>();
	} while(c != '\0');
	return ret;
}


} // namespace Lumix

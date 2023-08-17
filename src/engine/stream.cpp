#include "stream.h"
#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/string.h"


namespace Lumix {

OutputMemoryStream::OutputMemoryStream(IAllocator& allocator)
	: m_allocator(&allocator)
	, m_data(nullptr)
	, m_capacity(0)
	, m_size(0)
{}


OutputMemoryStream::OutputMemoryStream(void* data, u64 size)
	: m_data((u8*)data)
	, m_capacity(size)
	, m_allocator(nullptr)
	, m_size(0)
{
}


OutputMemoryStream::OutputMemoryStream(const OutputMemoryStream& blob, IAllocator& allocator)
	: m_allocator(&allocator)
	, m_size(blob.m_size)
{
	if (blob.m_capacity > 0)
	{
		m_data = (u8*)allocator.allocate(blob.m_capacity, 1);
		memcpy(m_data, blob.m_data, blob.m_capacity);
		m_capacity = blob.m_capacity;
	}
	else
	{
		m_data = nullptr;
		m_capacity = 0;
	}
}


OutputMemoryStream::OutputMemoryStream(const InputMemoryStream& blob, IAllocator& allocator)
	: m_allocator(&allocator)
	, m_size(blob.size())
{
	if (blob.size() > 0)
	{
		m_data = (u8*)allocator.allocate(blob.size(), 1);
		memcpy(m_data, blob.getData(), blob.size());
		m_capacity = blob.size();
	}
	else
	{
		m_data = nullptr;
		m_capacity = 0;
	}
}


OutputMemoryStream::~OutputMemoryStream()
{
	if (m_allocator) m_allocator->deallocate(m_data);
}


IOutputStream& IOutputStream::operator << (StringView str) {
	write(str.begin, str.size());
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
	m_size = rhs.m_size;
	m_capacity = rhs.m_capacity;
	m_data = rhs.m_data;
	
	rhs.m_data = nullptr;
	rhs.m_capacity = 0;
	rhs.m_size = 0;
}



OutputMemoryStream::OutputMemoryStream(const OutputMemoryStream& rhs)
{
	m_allocator = rhs.m_allocator;
	m_size = rhs.m_size;
	if (rhs.m_capacity > 0)
	{
		m_data = (u8*)m_allocator->allocate(rhs.m_capacity, 1);
		memcpy(m_data, rhs.m_data, rhs.m_capacity);
		m_capacity = rhs.m_capacity;
	}
	else
	{
		m_data = nullptr;
		m_capacity = 0;
	}
}


void OutputMemoryStream::operator =(const OutputMemoryStream& rhs)
{
	ASSERT(rhs.m_allocator);
	if (m_allocator) m_allocator->deallocate(m_data);
		
	m_allocator = rhs.m_allocator;
	m_size = rhs.m_size;
	if (rhs.m_capacity > 0)
	{
		m_data = (u8*)m_allocator->allocate(rhs.m_capacity, 1);
		memcpy(m_data, rhs.m_data, rhs.m_capacity);
		m_capacity = rhs.m_capacity;
	}
	else
	{
		m_data = nullptr;
		m_capacity = 0;
	}
}


void OutputMemoryStream::operator =(OutputMemoryStream&& rhs)
{
	ASSERT(rhs.m_allocator);
	if (m_allocator) m_allocator->deallocate(m_data);
		
	m_allocator = rhs.m_allocator;
	m_size = rhs.m_size;
	m_data = rhs.m_data;
	m_capacity = rhs.m_capacity;

	rhs.m_size = 0;
	rhs.m_capacity = 0;
	rhs.m_data = nullptr;
}
	

void* OutputMemoryStream::skip(u64 size)
{
	ASSERT(size > 0 || m_capacity > 0);

	if (m_size + size > m_capacity)
	{
		reserve((m_size + size) << 1);
	}
	void* ret = (u8*)m_data + m_size;
	m_size += size;
	return ret;
}

u8 OutputMemoryStream::operator[](u32 idx) const {
	ASSERT(idx < m_size);
	return m_data[idx];
}

u8& OutputMemoryStream::operator[](u32 idx) {
	ASSERT(idx < m_size);
	return m_data[idx];
}

static_assert(sizeof(OutputPagedStream::Page) == PageAllocator::PAGE_SIZE);

OutputPagedStream::OutputPagedStream(struct PageAllocator& allocator)
	: m_allocator(allocator)
{
	m_tail = m_head = new (NewPlaceholder(), m_allocator.allocate(true)) Page;
}

OutputPagedStream::~OutputPagedStream() {
	Page* p = m_head;
	while (p) {
		Page* tmp = p;
		p = p->next;
		m_allocator.deallocate(tmp, true);
	}
}

Span<u8> OutputPagedStream::reserve(u32 size) {
	if (m_tail->size == lengthOf(m_tail->data)) {
		Page* new_page = new (NewPlaceholder(), m_allocator.allocate(true)) Page;
		m_tail->next = new_page;
		m_tail = new_page;
	}

	u8* res = m_tail->data + m_tail->size;
	size = minimum(size, u32(sizeof(m_tail->data) - m_tail->size));
	m_tail->size += size;
	return Span(res, size);
}

bool OutputPagedStream::write(const void* data, u64 size) {
	const u8* src = (const u8*)data;
	while (size > 0) {
		Span<u8> dst = reserve((u32)size);
		memcpy(dst.begin(), data, dst.length());
		size -= dst.length();
	}
	return true;
}

InputPagedStream::InputPagedStream(const OutputPagedStream& src)
	: m_page(src.m_head)
{
}


bool InputPagedStream::read(void* buffer, u64 size) {
	u8* dst = (u8*)buffer;

	while (size > 0) {
		if (m_page_pos == m_page->size) {
			if (!m_page->next) return false;

			m_page_pos = 0;
			m_page = m_page->next;
		}
		const u32 chunk_size = minimum(u32(size), m_page->size - m_page_pos);
		memcpy(dst, m_page->data + m_page_pos, chunk_size);
		m_page_pos += chunk_size;
		size -= chunk_size;
	}
	return true;
}

bool OutputMemoryStream::write(const void* data, u64 size)
{
	if (!size) return true;

	if (m_size + size > m_capacity)
	{
		reserve((m_size + size) << 1);
	}
	memcpy((u8*)m_data + m_size, data, size);
	m_size += size;
	return true;
}


void OutputMemoryStream::writeString(StringView string) {
	const i32 size = string.size() + 1;
	write(string.begin, size - 1);
	write((char)0);
}


void OutputMemoryStream::clear()
{
	m_size = 0;
}


void OutputMemoryStream::free()
{
	m_allocator->deallocate(m_data);
	m_size = 0;
	m_capacity = 0;
	m_data = nullptr;
}


void OutputMemoryStream::reserve(u64 size)
{
	if (size <= m_capacity) return;

	ASSERT(m_allocator);
	u8* tmp = (u8*)m_allocator->allocate(size, 1);
	memcpy(tmp, m_data, m_capacity);
	m_allocator->deallocate(m_data);
	m_data = tmp;
	m_capacity = size;
}


Span<u8> OutputMemoryStream::releaseOwnership() {
	Span<u8> res((u8*)m_data, (u8*)m_data + m_capacity);
	m_data = nullptr;
	m_size = m_capacity = 0;
	return res;
}


void OutputMemoryStream::resize(u64 size)
{
	m_size = size;
	if (size <= m_capacity) return;

	ASSERT(m_allocator);
	u8* tmp = (u8*)m_allocator->allocate(size, 1);
	memcpy(tmp, m_data, m_capacity);
	m_allocator->deallocate(m_data);
	m_data = tmp;
	m_capacity = size;
}

InputMemoryStream::InputMemoryStream(Span<const u8> data)
	: m_data(data.begin())
	, m_size(data.length())
{}

InputMemoryStream::InputMemoryStream(const void* data, u64 size)
	: m_data((const u8*)data)
	, m_size(size)
{}

InputMemoryStream::InputMemoryStream(const OutputMemoryStream& blob)
	: m_data((const u8*)blob.data())
	, m_size(blob.size())
{}


void InputMemoryStream::set(const void* data, u64 size) {
	m_data = (u8*)data;
	m_size = size;
	m_pos = 0;
	m_has_overflow = false;
}


const void* InputMemoryStream::skip(u64 size)
{
	auto* pos = m_data + m_pos;
	m_pos += size;
	if (m_pos > m_size)
	{
		ASSERT(false);
		m_pos = m_size;
		m_has_overflow = true;
	}

	return (const void*)pos;
}


bool InputMemoryStream::read(void* data, u64 size)
{
	if (m_pos + (int)size > m_size)
	{
		for (i32 i = 0; i < size; ++i)
			((unsigned char*)data)[i] = 0;
		m_has_overflow = true;
		return false;
	}
	if (size)
	{
		memcpy(data, ((char*)m_data) + m_pos, size);
	}
	m_pos += size;
	return true;
}

void OutputMemoryStream::write(const String& string) {
	writeString(string);
}

bool InputMemoryStream::read(String& string)
{
	string = readString();
	return true;
}


const char* InputMemoryStream::readString()
{
	const char* ret = (const char*)m_data + m_pos;
	while (m_pos < m_size && m_data[m_pos]) ++m_pos;
	// TODO this should be runtime error, not assert
	if (m_pos >= m_size) {
		ASSERT(false);
		m_has_overflow = true;
		return nullptr;
	}
	++m_pos;
	return ret;
}


} // namespace Lumix

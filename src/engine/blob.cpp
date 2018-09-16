#include "engine/blob.h"
#include "engine/string.h"
#include <cstring>


namespace Lumix
{

	OutputBlob::OutputBlob(IAllocator& allocator)
		: m_allocator(&allocator)
		, m_data(nullptr)
		, m_size(0)
		, m_pos(0)
	{}


	OutputBlob::OutputBlob(void* data, int size)
		: m_data(data)
		, m_size(size)
		, m_allocator(nullptr)
		, m_pos(0)
	{
	}


	OutputBlob::OutputBlob(const OutputBlob& blob, IAllocator& allocator)
		: m_allocator(&allocator)
		, m_pos(blob.m_pos)
	{
		if (blob.m_size > 0)
		{
			m_data = allocator.allocate(blob.m_size);
			memcpy(m_data, blob.m_data, blob.m_size);
			m_size = blob.m_size;
		}
		else
		{
			m_data = nullptr;
			m_size = 0;
		}
	}


	OutputBlob::OutputBlob(const InputBlob& blob, IAllocator& allocator)
		: m_allocator(&allocator)
		, m_pos(blob.getSize())
	{
		if (blob.getSize() > 0)
		{
			m_data = allocator.allocate(blob.getSize());
			memcpy(m_data, blob.getData(), blob.getSize());
			m_size = blob.getSize();
		}
		else
		{
			m_data = nullptr;
			m_size = 0;
		}
	}


	OutputBlob::~OutputBlob()
	{
		if (m_allocator) m_allocator->deallocate(m_data);
	}


	OutputBlob& OutputBlob::operator << (const char* str)
	{
		write(str, stringLength(str));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (i32 value)
	{
		char tmp[20];
		toCString(value, tmp, lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (u64 value)
	{
		char tmp[40];
		toCString(value, tmp, lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (i64 value)
	{
		char tmp[40];
		toCString(value, tmp, lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (u32 value)
	{
		char tmp[20];
		toCString(value, tmp, lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (float value)
	{
		char tmp[30];
		toCString(value, tmp, lengthOf(tmp), 6);
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (double value)
	{
		char tmp[40];
		toCString(value, tmp, lengthOf(tmp), 12);
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob::OutputBlob(const OutputBlob& rhs)
	{
		m_allocator = rhs.m_allocator;
		m_pos = rhs.m_pos;
		if (rhs.m_size > 0)
		{
			m_data = m_allocator->allocate(rhs.m_size);
			memcpy(m_data, rhs.m_data, rhs.m_size);
			m_size = rhs.m_size;
		}
		else
		{
			m_data = nullptr;
			m_size = 0;
		}
	}


	void OutputBlob::operator =(const OutputBlob& rhs)
	{
		ASSERT(rhs.m_allocator);
		if (m_allocator) m_allocator->deallocate(m_data);
		
		m_allocator = rhs.m_allocator;
		m_pos = rhs.m_pos;
		if (rhs.m_size > 0)
		{
			m_data = m_allocator->allocate(rhs.m_size);
			memcpy(m_data, rhs.m_data, rhs.m_size);
			m_size = rhs.m_size;
		}
		else
		{
			m_data = nullptr;
			m_size = 0;
		}
	}
	

	void OutputBlob::write(const string& string)
	{
		writeString(string.c_str());
	}


	void* OutputBlob::skip(int size)
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


	void OutputBlob::write(const void* data, int size)
	{
		if (!size) return;

		if (m_pos + size > m_size)
		{
			reserve((m_pos + size) << 1);
		}
		memcpy((u8*)m_data + m_pos, data, size);
		m_pos += size;
	}


	void OutputBlob::writeString(const char* string)
	{
		if (string)
		{
			i32 size = stringLength(string) + 1;
			write(size);
			write(string, size);
		}
		else
		{
			write((i32)0);
		}
	}


	void OutputBlob::clear()
	{
		m_pos = 0;
	}


	void OutputBlob::reserve(int size)
	{
		if (size <= m_size) return;

		ASSERT(m_allocator);
		u8* tmp = (u8*)m_allocator->allocate(size);
		memcpy(tmp, m_data, m_size);
		m_allocator->deallocate(m_data);
		m_data = tmp;
		m_size = size;
	}


	void OutputBlob::resize(int size)
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



	InputBlob::InputBlob(const void* data, int size)
		: m_data((const u8*)data)
		, m_size(size)
		, m_pos(0)
	{}


	InputBlob::InputBlob(const OutputBlob& blob)
		: m_data((const u8*)blob.getData())
		, m_size(blob.getPos())
		, m_pos(0)
	{}


	const void* InputBlob::skip(int size)
	{
		auto* pos = m_data + m_pos;
		m_pos += size;
		if (m_pos > m_size)
		{
			m_pos = m_size;
		}

		return (const void*)pos;
	}


	bool InputBlob::read(void* data, int size)
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


	bool InputBlob::read(string& string)
	{
		i32 size;
		read(size);
		string.resize(size);
		bool res = read(string.getData(), size);
		return res;
	}


	bool InputBlob::readString(char* data, int max_size)
	{
		i32 size;
		read(size);
		ASSERT(size <= max_size);
		bool res = read(data, size < max_size ? size : max_size);
		for (int i = max_size; i < size; ++i)
		{
			readChar();
		}
		return res && size <= max_size;
	}


} // namespace Lumix

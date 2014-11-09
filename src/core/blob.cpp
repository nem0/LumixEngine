#include "core/blob.h"
#include <new>

namespace Lumix
{
	Blob::Blob(IAllocator& allocator)
		: m_allocator(allocator)
		, m_buffer(allocator)
	{
		m_pos = 0;
		m_size = 0;
	}


	Blob::Blob(const Blob& rhs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_buffer(allocator)
	{
		m_data = NULL;
		*this = rhs;
	}


	void Blob::operator =(const Blob& rhs)
	{
		m_data = rhs.m_data;
		m_pos = rhs.m_pos;
		m_size = rhs.m_size;
		m_buffer = rhs.m_buffer;
	}
	

	void Blob::rewindForRead()
	{
		m_pos = 0;
		if (!m_buffer.empty())
		{
			m_data = &m_buffer[0];
			m_size = m_buffer.size();
		}
	}


	void Blob::write(const void* data, int32_t size)
	{
		if(m_size + (int)size > m_buffer.size())
		{
			m_buffer.resize(m_size + size);
			m_data = &m_buffer[0];
		}
		if (size)
		{
			memcpy(&m_buffer[0] + m_size, data, size);
		}
		m_size += size;
	}


	bool Blob::read(void* data, int32_t size)
	{
		if(m_pos + (int)size > m_size)
		{
			for(int32_t i = 0; i < size; ++i)
				((unsigned char*)data)[i] = 0;	
			return false;
		}
		memcpy(data, ((char*)m_data) + m_pos, size);
		m_pos += size;	
		return true;
	}

	void Blob::write(const char* string)
	{
		int32_t size = (int32_t)strlen(string) + 1;

		write(size);
		write(string, size);
	}
} // !namespace Lumix

#include "core/blob.h"
#include <cstring>
#include <new>

namespace Lumix
{
/*	OutputBlob::OutputBlob(IAllocator& allocator)
		: m_allocator(allocator)
		, m_own_data(allocator)
	{
		m_pos = 0;
		m_size = 0;
	}


	OutputBlob::OutputBlob(const OutputBlob& rhs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_own_data(allocator)
	{
		m_data = NULL;
		*this = rhs;
	}


	void OutputBlob::operator =(const OutputBlob& rhs)
	{
		m_data = rhs.m_data;
		m_pos = rhs.m_pos;
		m_size = rhs.m_size;
		m_own_data = rhs.m_own_data;
	}
	

	void OutputBlob::rewindForRead()
	{
		m_pos = 0;
		if (!m_own_data.empty())
		{
			m_data = &m_own_data[0];
			m_size = m_own_data.size();
		}
	}


	void OutputBlob::write(const void* data, int32_t size)
	{
		if(m_size + (int)size > m_own_data.size())
		{
			m_own_data.resize(m_size + size);
			m_data = &m_own_data[0];
		}
		if (size)
		{
			memcpy(&m_own_data[0] + m_size, data, size);
		}
		m_size += size;
	}


	bool OutputBlob::read(void* data, int32_t size)
	{
		if(m_pos + (int)size > m_size)
		{
			for(int32_t i = 0; i < size; ++i)
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


	void OutputBlob::readString(char* out, int max_size)
	{
		int32_t size;
		read(size);
		ASSERT(size <= max_size);
		read(out, size < max_size ? size : max_size);
	}


	void OutputBlob::writeString(const char* string)
	{
		int32_t size = (int32_t)strlen(string) + 1;

		write(size);
		write(string, size);
	}*/

	OutputBlob::OutputBlob(IAllocator& allocator)
		: m_data(allocator)
	{}


	OutputBlob::OutputBlob(const OutputBlob& blob, IAllocator& allocator)
		: m_data(allocator)
	{
		m_data = blob.m_data;
	}


	void OutputBlob::operator =(const OutputBlob& rhs)
	{
		m_data = rhs.m_data;
	}


	OutputBlob::OutputBlob(const OutputBlob& rhs)
		: m_data(rhs.m_data)
	{}


	void OutputBlob::write(const void* data, int size)
	{
		if (size)
		{
			int pos = m_data.size();
			m_data.resize(m_data.size() + size);
			memcpy(&m_data[0] + pos, data, size);
		}
	}


	void OutputBlob::writeString(const char* string)
	{
		int32_t size = (int32_t)strlen(string) + 1;

		write(size);
		write(string, size);
	}


	InputBlob::InputBlob(const void* data, int size)
		: m_data((const uint8_t*)data)
		, m_size(size)
		, m_pos(0)
	{}


	InputBlob::InputBlob(const OutputBlob& blob)
		: m_data((const uint8_t*)blob.getData())
		, m_size(blob.getSize())
		, m_pos(0)
	{}


	bool InputBlob::read(void* data, int size)
	{
		if (m_pos + (int)size > m_size)
		{
			for (int32_t i = 0; i < size; ++i)
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
	
	
	bool InputBlob::readString(char* data, int max_size)
	{
		int32_t size;
		read(size);
		ASSERT(size <= max_size);
		return read(data, size < max_size ? size : max_size);
	}


} // !namespace Lumix

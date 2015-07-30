#include "core/blob.h"


namespace Lumix
{

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
		if (string)
		{
			int32_t size = (int32_t)strlen(string) + 1;
			write(size);
			write(string, size);
		}
		else
		{
			write((int32_t)0);
		}
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

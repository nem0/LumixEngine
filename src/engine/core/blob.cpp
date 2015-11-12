#include "core/blob.h"
#include "core/string.h"


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


	OutputBlob& OutputBlob::operator << (const char* str)
	{
		write(str, stringLength(str));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (int value)
	{
		char tmp[20];
		Lumix::toCString(value, tmp, Lumix::lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (uint32 value)
	{
		char tmp[20];
		Lumix::toCString(value, tmp, Lumix::lengthOf(tmp));
		write(tmp, stringLength(tmp));
		return *this;
	}


	OutputBlob& OutputBlob::operator << (float value)
	{
		char tmp[30];
		Lumix::toCString(value, tmp, Lumix::lengthOf(tmp), 6);
		write(tmp, stringLength(tmp));
		return *this;
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
			copyMemory(&m_data[0] + pos, data, size);
		}
	}


	void OutputBlob::writeString(const char* string)
	{
		if (string)
		{
			int32 size = stringLength(string) + 1;
			write(size);
			write(string, size);
		}
		else
		{
			write((int32)0);
		}
	}


	InputBlob::InputBlob(const void* data, int size)
		: m_data((const uint8*)data)
		, m_size(size)
		, m_pos(0)
	{}


	InputBlob::InputBlob(const OutputBlob& blob)
		: m_data((const uint8*)blob.getData())
		, m_size(blob.getSize())
		, m_pos(0)
	{}


	bool InputBlob::read(void* data, int size)
	{
		if (m_pos + (int)size > m_size)
		{
			for (int32 i = 0; i < size; ++i)
				((unsigned char*)data)[i] = 0;
			return false;
		}
		if (size)
		{
			copyMemory(data, ((char*)m_data) + m_pos, size);
		}
		m_pos += size;
		return true;
	}
	
	
	bool InputBlob::readString(char* data, int max_size)
	{
		int32 size;
		read(size);
		ASSERT(size <= max_size);
		return read(data, size < max_size ? size : max_size);
	}


} // !namespace Lumix

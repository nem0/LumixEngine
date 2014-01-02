#include "core/blob.h"
#include <new>

namespace Lux
{


Blob::Blob()
{
	m_pos = 0;
	m_size = 0;
}

	
void Blob::write(const void* data, size_t size)
{
	if(m_size + (int)size > m_buffer.size())
	{
		m_buffer.resize(m_size + size);
	}
	memcpy(&m_buffer[0] + m_size, data, size);
	m_size += size;
}


bool Blob::read(void* data, size_t size)
{
	if(m_pos + (int)size > m_size)
	{
		for(size_t i = 0; i < size; ++i)
			((unsigned char*)data)[i] = 0;	
		return false;
	}
	memcpy(data, ((char*)m_data) + m_pos, size);
	m_pos += size;	
	return true;
}


} // !namespace Lux
#include "core/fifo_allocator.h"
#include "core/mt/atomic.h"

namespace Lumix
{
	FIFOAllocator::FIFOAllocator(size_t buffer_size)
		: m_mutex(false)
	{
		m_buffer_size = buffer_size;
		m_buffer = static_cast<uint8*>(malloc(buffer_size));
		m_start = m_end = 0;
	}

	FIFOAllocator::~FIFOAllocator()
	{
		ASSERT(m_start == m_end);
		free(m_buffer);
	}

	void* FIFOAllocator::allocate(size_t n)
	{
		ASSERT(n + 4 < m_buffer_size);
		MT::SpinLock lock(m_mutex);
		
		int32 size = (int32)n + 4;
		int32 old_end = m_end;
		int32 new_end;
		if (old_end + size > (int32)m_buffer_size)
		{
			new_end = size;
		}
		else
		{
			new_end = old_end + size;
		}
		if(old_end < m_start && new_end >= m_start)
		{
			ASSERT(false); /// out of memory
			return nullptr;
		}
		*(int32*)(m_buffer + (new_end - size)) = (int32)n;
		m_end = new_end;
		return m_buffer + (new_end - size) + 4;
	}

	void FIFOAllocator::deallocate(void* p)
	{
		MT::SpinLock lock(m_mutex);
		int32 n = *(((int32*)p) - 1);
		m_start = n + (int32)((uint8*)p - m_buffer);
	}


} // ~namespace Lumix

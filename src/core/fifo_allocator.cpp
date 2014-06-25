#include "core/fifo_allocator.h"
#include "core/MT/atomic.h"

namespace Lumix
{
	struct AllocationHeader
	{
		uint8_t* m_next;
		size_t m_size;
	};

	FIFOAllocator::FIFOAllocator(size_t buffer_size)
		: m_mutex(false)
	{
		m_buffer_size = buffer_size;
		m_buffer = static_cast<uint8_t*>(LUMIX_MALLOC(buffer_size));
		m_start = m_end = 0;
	}

	FIFOAllocator::~FIFOAllocator()
	{
		ASSERT(m_start == m_end);
		LUMIX_FREE(m_buffer);
	}

	void* FIFOAllocator::allocate(size_t n)
	{
		ASSERT(n + 4 < m_buffer_size);
		MT::SpinLock lock(m_mutex);
		
		int32_t size = (int32_t)n + 4;
		int32_t old_end = m_end;
		int32_t new_end;
		if (old_end + size > (int32_t)m_buffer_size)
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
			return NULL;
		}
		*(int32_t*)(m_buffer + (new_end - size)) = (int32_t)n;
		m_end = new_end;
		return m_buffer + (new_end - size) + 4;
	}

	void FIFOAllocator::deallocate(void* p)
	{
		MT::SpinLock lock(m_mutex);
		int32_t n = *(((int32_t*)p) - 1);
		m_start = n + (int32_t)((uint8_t*)p - m_buffer);
	}

	void* FIFOAllocator::reallocate(void*, size_t)
	{
		ASSERT(false); // not supported
		return NULL;
	}


} // ~namespace Lumix

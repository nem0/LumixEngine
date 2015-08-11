#pragma once

#include "lumix.h"

namespace Lumix
{
	template<class T, int32_t chunk_size>
	class FreeList : public IAllocator
	{
	public:
		explicit FreeList(IAllocator& allocator)
			: m_allocator(allocator)
		{
			m_heap = static_cast<T*>(allocator.allocate(sizeof(T) * chunk_size));
			m_pool_index = chunk_size;

			for (int32_t i = 0; i < chunk_size; i++)
			{
				m_pool[i] = &m_heap[i];
			}
		}

		~FreeList()
		{
			m_allocator.deallocate(m_heap);
		}

		void* allocate(size_t size) override
		{
			ASSERT(size == sizeof(T));
			return m_pool_index > 0 ? m_pool[--m_pool_index] : nullptr;
		}

		void deallocate(void* ptr) override
		{
			ASSERT(((uintptr_t)ptr >= (uintptr_t)&m_heap[0]) && ((uintptr_t)ptr < (uintptr_t)&m_heap[chunk_size]));
			m_pool[m_pool_index++] = reinterpret_cast<T*>(ptr);
		}

	private:
		IAllocator&	m_allocator;
		int32_t		m_pool_index;
		T*			m_pool[chunk_size];
		T*			m_heap;
	};

	template<int32_t chunk_size>
	class FreeList<int32_t, chunk_size>
	{
	public:
		FreeList()
		{
			m_pool_index = chunk_size;

			for (int32_t i = 0; i < chunk_size; i++)
			{
				m_pool[i] = i;
			}
		}

		int32_t alloc(void)
		{
			return m_pool_index > 0 ? m_pool[--m_pool_index] : (-1);
		}

		void release(int32_t id)
		{
			ASSERT (id >= 0 && id < chunk_size);
			m_pool[m_pool_index++] = id;
		}

	private:
		int32_t		m_pool_index;
		int32_t		m_pool[chunk_size];
	};
} // ~namespace Lumix

#pragma once

#include "engine/lumix.h"
#include "engine/iallocator.h"

namespace Lumix
{
	template<class T, i32 chunk_size>
	class FreeList : public IAllocator
	{
	public:
		explicit FreeList(IAllocator& allocator)
			: m_allocator(allocator)
		{
			m_heap = static_cast<T*>(allocator.allocate_aligned(sizeof(T) * chunk_size, ALIGN_OF(T)));
			m_pool_index = chunk_size;

			for (i32 i = 0; i < chunk_size; i++)
			{
				m_pool[i] = &m_heap[i];
			}
		}

		~FreeList()
		{
			m_allocator.deallocate_aligned(m_heap);
		}

		void* allocate(size_t size) override
		{
			ASSERT(size == sizeof(T));
			return m_pool_index > 0 ? m_pool[--m_pool_index] : nullptr;
		}

		void deallocate(void* ptr) override
		{
			ASSERT(((uintptr)ptr >= (uintptr)&m_heap[0]) && ((uintptr)ptr < (uintptr)&m_heap[chunk_size]));
			m_pool[m_pool_index++] = reinterpret_cast<T*>(ptr);
		}

		void* reallocate(void*, size_t) override
		{
			ASSERT(false);
			return nullptr;
		}

		void* allocate_aligned(size_t size, size_t align) override
		{
			void* ptr = allocate(size);
			ASSERT((uintptr)ptr % align == 0);
			return ptr;
		}


		void deallocate_aligned(void* ptr) override
		{
			return deallocate(ptr);
		}


		void* reallocate_aligned(void* ptr, size_t size, size_t align) override
		{
			ASSERT(size <= ALIGN_OF(T));
			return reallocate(ptr, size);
		}


	private:
		IAllocator&	m_allocator;
		i32		m_pool_index;
		T*			m_pool[chunk_size];
		T*			m_heap;
	};

	template<i32 chunk_size>
	class FreeList<i32, chunk_size>
	{
	public:
		FreeList()
		{
			m_pool_index = chunk_size;

			for (i32 i = 0; i < chunk_size; i++)
			{
				m_pool[i] = i;
			}
		}

		i32 alloc(void)
		{
			return m_pool_index > 0 ? m_pool[--m_pool_index] : (-1);
		}

		void release(i32 id)
		{
			ASSERT (id >= 0 && id < chunk_size);
			m_pool[m_pool_index++] = id;
		}

	private:
		i32		m_pool_index;
		i32		m_pool[chunk_size];
	};
} // ~namespace Lumix

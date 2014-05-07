#pragma once

#include "core/lux.h"

namespace Lux
{
	template<class T, int32_t chunk_size, int32_t align_of = sizeof(double)>
	class FreeList
	{
	public:
		FreeList()
		{
			m_heap = static_cast<T*>(LUX_NEW_ARRAY(char, sizeof(T) * chunk_size));
			m_pool_index = chunk_size;

			for (int32_t i = 0; i < chunk_size; i++)
			{
				m_pool[i] = &m_heap[i];
			}
		}

		~FreeList()
		{
			LUX_DELETE_ARRAY(m_heap);
		}

		LUX_FORCE_INLINE T* alloc(void)
		{
			T* p = NULL;
			if (m_pool_index > 0)
			{
				p = m_pool[--m_pool_index];
				new (p) T();
			}
			return p;
		}

		template<typename P1>
		LUX_FORCE_INLINE T* alloc(P1 p1)
		{
			T* p = NULL;
			if (m_pool_index > 0)
			{
				p = m_pool[--m_pool_index];
				new (p) T(p1);
			}
			return p;
		}

		template<typename P1, typename P2>
		LUX_FORCE_INLINE T* alloc(P1 p1, P2 p2)
		{
			T* p = NULL;
			if (m_pool_index > 0)
			{
				p = m_pool[--m_pool_index];
				new (p) T(p1, p2);
			}
			return p;
		}

		template<typename P1, typename P2, typename P3>
		LUX_FORCE_INLINE T* alloc(P1 p1, P2 p2, P3 p3)
		{
			T* p = NULL;
			if (m_pool_index > 0)
			{
				p = m_pool[--m_pool_index];
				new (p) T(p1, p2, p3);
			}
			return p;
		}

		LUX_FORCE_INLINE void release(T* p)
		{
			ASSERT (((uintptr_t)p >= (uintptr_t)&m_heap[0]) && ((uintptr_t)p < (uintptr_t)&m_heap[chunk_size]));
			p->~T();
			m_pool[m_pool_index++] = p;
		}

	private:
		int32_t		m_pool_index;
		T*			m_pool[chunk_size];
		T*			m_heap;
	};

	template<int32_t chunk_size>
	class FreeList<int32_t, chunk_size, 8>
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

		LUX_FORCE_INLINE int32_t alloc(void)
		{
			return m_pool_index > 0 ? m_pool[--m_pool_index] : (-1);
		}

		LUX_FORCE_INLINE void release(int32_t id)
		{
			ASSERT (id >= 0 && id < chunk_size);
			m_pool[m_pool_index++] = id;
		}

	private:
		int32_t		m_pool_index;
		int32_t		m_pool[chunk_size];
	};
} // ~namespace Lux

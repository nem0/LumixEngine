#pragma once

#include "engine/core/math_utils.h"


namespace Lumix
{
	template <typename T, uint32 count>
	class Queue
	{
	public:
		Queue(IAllocator& allocator)
			: m_allocator(allocator)
		{
			ASSERT(Math::isPowOfTwo(count));
			m_buffer = (T*)(m_allocator.allocate(sizeof(T) * count));
			m_wr = m_rd = 0;
		}

		~Queue()
		{
			m_allocator.deallocate(m_buffer);
		}

		bool empty() const { return m_rd == m_wr; } 
		uint32 size() const { return m_wr - m_rd; }

		void push(const T& item)
		{
			ASSERT(m_wr - m_rd < count);

			uint32 idx = m_wr & (count - 1);
			::new (NewPlaceholder(), &m_buffer[idx]) T(item);
			++m_wr;
		}

		void pop()
		{
			ASSERT(m_wr != m_rd);

			uint32 idx = m_rd & (count - 1);
			(&m_buffer[idx])->~T();
			m_rd++;
		}

		T& front()
		{
			uint32 idx = m_rd & (count - 1);
			return m_buffer[idx];
		}

		const T& front() const
		{
			uint32 idx = m_rd & (count - 1);
			return m_buffer[idx];
		}

		T& back()
		{
			ASSERT(!empty());

			uint32 idx = m_wr & (count - 1);
			return m_buffer[idx - 1];
		}

		const T& back() const
		{
			ASSERT(!empty());

			uint32 idx = m_wr & (count - 1);
			return m_buffer[idx - 1];
		}

	private:
		IAllocator& m_allocator;
		uint32 m_rd;
		uint32 m_wr;
		T* m_buffer;
	};
}

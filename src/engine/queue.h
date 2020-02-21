#pragma once


#include "engine/allocator.h"


namespace Lumix
{
	template <typename T, u32 COUNT>
	struct Queue
	{
		static_assert(COUNT && !(COUNT & (COUNT - 1)), "Is not power of 2");
	public:
		struct Iterator
		{
			Queue* owner;
			u32 cursor;

			bool operator!=(const Iterator& rhs) const { return cursor != rhs.cursor || owner != rhs.owner; }
			void operator ++() { ++cursor; }
			T& value() { u32 idx = cursor & (COUNT - 1); return owner->m_buffer[idx]; }
		};

		explicit Queue() {
			m_buffer = (T*)m_mem;
			m_wr = m_rd = 0;
		}

		bool full() const { return size() == COUNT; }
		bool empty() const { return m_rd == m_wr; } 
		u32 size() const { return m_wr - m_rd; }
		Iterator begin() { return {this, m_rd}; }
		Iterator end() { return {this, m_wr}; }

		void push(const T& item)
		{
			ASSERT(m_wr - m_rd < COUNT);

			u32 idx = m_wr & (COUNT - 1);
			::new (NewPlaceholder(), &m_buffer[idx]) T(item);
			++m_wr;
		}

		void pop()
		{
			ASSERT(m_wr != m_rd);

			u32 idx = m_rd & (COUNT - 1);
			(&m_buffer[idx])->~T();
			m_rd++;
		}

		T& front()
		{
			u32 idx = m_rd & (COUNT - 1);
			return m_buffer[idx];
		}

		const T& front() const
		{
			u32 idx = m_rd & (COUNT - 1);
			return m_buffer[idx];
		}

		T& back()
		{
			ASSERT(!empty());

			u32 idx = m_wr & (COUNT - 1);
			return m_buffer[idx - 1];
		}

		const T& back() const
		{
			ASSERT(!empty());

			u32 idx = m_wr & (COUNT - 1);
			return m_buffer[idx - 1];
		}

	private:
		u32 m_rd;
		u32 m_wr;
		T* m_buffer = (T*)m_mem;
		alignas(T) u8 m_mem[sizeof(T) * COUNT];
	};
}

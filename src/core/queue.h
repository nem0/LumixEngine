#pragma once

#include <new>

namespace Lux
{
	template <typename T, uint32_t count>
	class Queue
	{
	public:
		Queue()
		{
			m_buffer = (T*)new char[sizeof(T) * count];
			m_wr = m_rd = 0;
		}

		~Queue()
		{
			delete [] m_buffer;
		}

		bool empty() const { return m_rd == m_wr; } 
		uint32_t size() const { return m_wr - m_rd; }

		void push(const T& item)
		{
			ASSERT(m_wr - m_rd < count);

			uint32_t idx = m_wr & (count - 1);
			::new (&m_buffer[idx]) T(item);
			++m_wr;
		}

		void pop()
		{
			ASSERT(m_wr != m_rd);

			uint32_t idx = m_rd & (count - 1);
			(&m_buffer[idx])->~T();
			m_rd++;
		}

		T& front()
		{
			uint32_t idx = m_rd & (count - 1);
			return m_buffer[idx];
		}

		const T& front() const
		{
			uint32_t idx = m_rd & (count - 1);
			return m_buffer[idx];
		}

		T& back()
		{
			ASSERT(!empty());

			uint32_t idx = m_wr & (count - 1);
			return m_buffer[idx - 1];
		}

		const T& back() const
		{
			ASSERT(!empty());

			uint32_t idx = m_wr & (count - 1);
			return m_buffer[idx - 1];
		}

	private:
		uint32_t m_rd;
		uint32_t m_wr;
		T* m_buffer;
	};
}

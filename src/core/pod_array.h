#pragma once


#include <cstdlib>
#include "core/default_allocator.h"



namespace Lux
{


template <typename T, typename Allocator = DefaultAllocator>
class PODArray
{
	public:
		explicit PODArray(const Allocator& allocator)
			: m_allocator(allocator)
		{
			m_data = NULL;
			m_capacity = 0;
			m_size = 0;
		}

		explicit PODArray(const PODArray& rhs)
		{
			m_data = NULL;
			m_capacity = 0;
			m_size = 0;
			*this = rhs;
		}

		void operator =(const PODArray& rhs)
		{
			m_allocator.deallocate(m_data);
			m_data = (T*)m_allocator.allocate(rhs.m_capacity * sizeof(T));
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			memmove(m_data, rhs.m_data, sizeof(T) * m_size);
		}

		PODArray()
		{
			m_data = NULL;
			m_capacity = 0;
			m_size = 0;
		}

		~PODArray()
		{
			m_allocator.deallocate(m_data);
		}

		void eraseFast(int index)
		{
			if(index >= 0 && index < m_size)
			{
				if(index != m_size - 1)
				{
					memmove(m_data + index, m_data + m_size - 1, sizeof(T));
				}
				--m_size;
			}
		}

		void erase(int index)
		{
			if(index >= 0 && index < m_size)
			{
				memmove(m_data + index, m_data + index + 1, sizeof(T) * (m_size - index - 1));
				--m_size;
			}
		}

		void push(const T& value)
		{
			if(m_size == m_capacity)
			{
				grow();
			}
			m_data[m_size] = value;
			++m_size;
		}

		bool empty() const { return m_size == 0; }

		void clear()
		{
			m_size = 0;
		}

		T& pushEmpty()
		{
			if(m_size == m_capacity)
			{
				grow();
			}
			++m_size;
			return m_data[m_size-1];
		}


		const T& back() const
		{
			return m_data[m_size-1];
		}


		T& back()
		{
			return m_data[m_size-1];
		}


		void pop()
		{
			if(m_size > 0)
			{
				--m_size;
			}
		}

		void resize(int size)
		{
			if(size > m_capacity)
			{
				reserve(size);
			}
			m_size = size;
		}

		void reserve(int capacity)
		{
			if(capacity > m_capacity)
			{
				T* newData = (T*)m_allocator.allocate(capacity * sizeof(T));
				memmove(newData, m_data, sizeof(T) * m_size);
				m_allocator.deallocate(m_data);
				m_data = newData;
				m_capacity = capacity;			
			}
		}

		const T& operator[] (int index) const { ASSERT(index < m_size); return m_data[index]; }
		T& operator[](int index) { return m_data[index]; }
 		int size() const { return m_size; }
		int capacity() const { return m_capacity; }

	private:
		void* operator &() { return 0; }

		void grow()
		{
			int newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
			m_data = (T*)m_allocator.reallocate(m_data, newCapacity * sizeof(T));
			m_capacity = newCapacity;
		}

	private:
		int m_capacity;
		int m_size;
		T* m_data;
		Allocator m_allocator;
};


} // ~namespace Lux
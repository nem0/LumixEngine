#pragma once


#include <cstdlib>
#include <new>
#include "core/default_allocator.h"



namespace Lux
{


template <typename T, typename Allocator = DefaultAllocator>
class vector
{
	public:
		vector(const Allocator& allocator)
			: m_allocator(allocator)
		{
			m_data = NULL;
			m_capacity = 0;
			m_size = 0;
		}

		vector()
		{
			m_data = NULL;
			m_capacity = 0;
			m_size = 0;
		}

		/*vector(const vector<T>& rhs)
		{
			m_capacity = 0;
			m_size = 0;
			m_data = 0;
			reserve(rhs.m_capacity);
			for(int i = 0; i < rhs.m_size; ++i)
			{
				new ((char*)(m_data+i)) T(rhs.m_data[i]);
			}
			m_size = rhs.m_size;
		}*/

		~vector()
		{
			callDestructors(m_data, m_data + m_size);
			m_allocator.deallocate(m_data, m_capacity * sizeof(T));
		}

		void eraseFast(int index)
		{
			if(index >= 0 && index < m_size)
			{
				m_data[index].~T();
				if(index != m_size - 1)
				{
					new ((char*)(m_data+index)) T(m_data[m_size - 1]);
				}
				--m_size;
			}
		}

		void erase(int index)
		{
			if(index >= 0 && index < m_size)
			{
				m_data[index].~T();
				for(int i = index + 1; i < m_size; ++i)
				{
					new ((char*)(m_data+i-1)) T(m_data[i]);
					m_data[i].~T();
				}
				--m_size;
			}
		}

		void push_back(const T& value)
		{
			if(m_size == m_capacity)
			{
				grow();
			}
			new ((char*)(m_data+m_size)) T(value);
			++m_size;
		}

		bool empty() const { return m_size == 0; }

		void clear()
		{
			callDestructors(m_data, m_data + m_size);
			m_size = 0;
		}

		T& push_back_empty()
		{
			if(m_size == m_capacity)
			{
				grow();
			}
			new ((char*)(m_data+m_size)) T();
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


		void pop_back()
		{
			if(m_size > 0)
			{
				m_data[m_size-1].~T();
				--m_size;
			}
		}

		/// TODO remove this when we have PODArray
		void set_size(int size)
		{
			if(size <= m_capacity)
			{
				m_size = size;
			}
		}

		void resize(int size)
		{
			if(size > m_capacity)
			{
				reserve(size);
			}
			for(int i = m_size; i < size; ++i)
			{
				new ((char*)(m_data+i)) T();
			}
			callDestructors(m_data + size, m_data + m_size);
			m_size = size;
		}

		void reserve(int capacity)
		{
			if(capacity > m_capacity)
			{
				T* newData = (T*)m_allocator.allocate(capacity * sizeof(T));
				for(int i = 0; i < m_size; ++i)
				{
					new ((char*)(newData+i)) T(m_data[i]);
				}
				callDestructors(m_data, m_data + m_size);
				m_allocator.deallocate(m_data, m_capacity * sizeof(T));
				m_data = newData;
				m_capacity = capacity;			
			}
		}

		const T& operator[] (int index) const { ASSERT(index < m_size); return m_data[index]; }
		T& operator[](int index) { return m_data[index]; }
 		int size() const { return m_size; }
		int capacity() const { return m_capacity; }

		/*void operator =(const vector<T>& rhs) 
		{
			callDestructors(m_data, m_data + m_size);
			if(m_capacity < rhs.m_size)
			{
				delete[] m_data;
				m_data = (T*)new char[rhs.m_size * sizeof(T)];
				m_capacity = rhs.m_size;
			}
			m_size = rhs.m_size;
			if(m_size > 0)
			{
				for(int i = 0; i < rhs.m_size; ++i)
				{
					new ((char*)(m_data + i)) T(rhs.m_data[i]);
				}
			}
		}*/
	private:
		void* operator &() { return 0; }

		void grow()
		{
			int newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
			T* newData = (T*)m_allocator.allocate(newCapacity * sizeof(T));
			for(int i = 0; i < m_size; ++i)
			{
				new ((char*)(newData + i)) T(m_data[i]);
			}
			callDestructors(m_data, m_data + m_size);
			m_allocator.deallocate(m_data, m_capacity * sizeof(T));
			m_data = newData;
			m_capacity = newCapacity;
		}

		void callDestructors(T* begin, T* end)
		{
			for(; begin < end; ++begin)
			{
				begin->~T();
			}
		}

	private:
		int m_capacity;
		int m_size;
		T* m_data;
		Allocator m_allocator;
};


} // ~namespace Lux
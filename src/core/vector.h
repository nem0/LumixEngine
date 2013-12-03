#pragma once


#include "core/lux.h"
#include <cstdlib>
#include <new>


namespace Lux
{


template <class T>
class vector
{
	public:
		vector()
		{
			m_data = 0;
			m_capacity = 0;
			m_size = 0;
		}

		vector(const vector<T>& rhs)
		{
			m_capacity = 0;
			m_size = 0;
			m_data = 0;
			reserve(rhs.m_capacity);
			memcpy(m_data, rhs.m_data, sizeof(T) * rhs.m_size);
			m_size = rhs.m_size;
		}

		~vector()
		{
			for(int i = 0; i < m_size; ++i)
			{
				m_data[i].~T();
			}
			delete[] (char*)m_data;
		}

		void eraseFast(int index)
		{
			if(index >= 0 && index < m_size)
			{
				m_data[index].~T();
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
				m_data[index].~T();
				memmove(m_data + index, m_data + index + 1, sizeof(T) * (m_size - index - 1));
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
			for(int i = 0; i < m_size; ++i)
			{
				m_data[i].~T();
			}
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
			for(int i = size; i < m_size; ++i)
			{
				m_data[i].~T();
			}
			m_size = size;
		}

		void reserve(int capacity)
		{
			if(capacity > m_capacity)
			{
				T* newData = (T*)new char[capacity * sizeof(T)];
				memcpy(newData, m_data, m_size * sizeof(T));
				delete[] ((char*)m_data);
				m_data = newData;
				m_capacity = capacity;			
			}
		}

		const T& operator[] (int index) const { assert(index < m_size); return m_data[index]; }
		T& operator[](int index) { return m_data[index]; }
 		int size() const { return m_size; }
		int capacity() const { return m_capacity; }

		void operator =(const vector<T>& rhs) 
		{
			if(m_capacity < rhs.m_size)
			{
				delete[] m_data;
				m_data = (T*)new char[rhs.m_size * sizeof(T)];
				m_capacity = rhs.m_size;
			}
			m_size = rhs.m_size;
			if(m_size > 0)
			{
				memcpy(m_data, rhs.m_data, sizeof(T) * rhs.m_size);
			}
		}
	private:
		void* operator &() { return 0; }

		void grow()
		{
			int newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
			T* newData = (T*)new char[newCapacity * sizeof(T)];
			memcpy(newData, m_data, m_size * sizeof(T));
			delete[] ((char*)m_data);
			m_data = newData;
			m_capacity = newCapacity;
		}

	private:
		int m_capacity;
		int m_size;
		T* m_data;
};


} // ~namespace Lux
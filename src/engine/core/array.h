#pragma once


#include "core/iallocator.h"
#include <new>


namespace Lumix
{


template <typename T, bool is_trivially_copyable = std::is_trivial<T>::value>
class Array;


template <typename T> class Array<T, false>
{
public:
	explicit Array(IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
	}

	explicit Array(const Array& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
		*this = rhs;
	}


	T* begin() const { return m_data; }


	T* end() const { return m_data + m_size; }


	void removeDuplicates()
	{
		for (int i = 0; i < m_size-1; ++i)
		{
			for (int j = i + 1; j < m_size; ++j)
			{
				if (m_data[i] == m_data[j])
				{
					eraseFast(j);
					--j;
				}
			}
		}
	}


	void operator=(const Array& rhs)
	{
		if (this != &rhs)
		{
			callDestructors(m_data, m_data + m_size);
			m_allocator.deallocate(m_data);
			m_data = (T*)m_allocator.allocate(rhs.m_capacity * sizeof(T));
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			for (int i = 0; i < m_size; ++i)
			{
				new ((char*)(m_data + i)) T(rhs.m_data[i]);
			}
		}
	}

	~Array()
	{
		callDestructors(m_data, m_data + m_size);
		m_allocator.deallocate(m_data);
	}

	int indexOf(const T& item)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				return i;
			}
		}
		return -1;
	}

	void eraseItemFast(const T& item)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				eraseFast(i);
				return;
			}
		}
	}

	void eraseFast(int index)
	{
		if (index >= 0 && index < m_size)
		{
			m_data[index].~T();
			if (index != m_size - 1)
			{
				new ((char*)(m_data + index)) T(m_data[m_size - 1]);
			}
			--m_size;
		}
	}

	void eraseItem(const T& item)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				erase(i);
				return;
			}
		}
	}


	void insert(int index, const T& value)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		memmove(
			m_data + index + 1, m_data + index, sizeof(T) * (m_size - index));
		new (&m_data[index]) T(value);
		++m_size;
	}


	void erase(int index)
	{
		if (index >= 0 && index < m_size)
		{
			m_data[index].~T();
			if (index < m_size - 1)
			{
				memmove(m_data + index,
						m_data + index + 1,
						sizeof(T) * (m_size - index - 1));
			}
			--m_size;
		}
	}

	void push(const T& value)
	{
		int size = m_size;
		if (size == m_capacity)
		{
			grow();
		}
		new ((char*)(m_data + size)) T(value);
		++size;
		m_size = size;
	}

	template <typename... Params> T& emplace(Params&&... params)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		new ((char*)(m_data + m_size)) T(std::forward<Params>(params)...);
		++m_size;
		return m_data[m_size - 1];
	}

	bool empty() const { return m_size == 0; }

	void clear()
	{
		callDestructors(m_data, m_data + m_size);
		m_size = 0;
	}

	T& pushEmpty()
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		new ((char*)(m_data + m_size)) T();
		++m_size;
		return m_data[m_size - 1];
	}

	const T& back() const { return m_data[m_size - 1]; }


	T& back() { return m_data[m_size - 1]; }


	void pop()
	{
		if (m_size > 0)
		{
			m_data[m_size - 1].~T();
			--m_size;
		}
	}

	void resize(int size)
	{
		if (size > m_capacity)
		{
			reserve(size);
		}
		for (int i = m_size; i < size; ++i)
		{
			new ((char*)(m_data + i)) T();
		}
		callDestructors(m_data + size, m_data + m_size);
		m_size = size;
	}

	void reserve(int capacity)
	{
		if (capacity > m_capacity)
		{
			T* newData = (T*)m_allocator.allocate(capacity * sizeof(T));
			memcpy(newData, m_data, sizeof(T) * m_size);
			m_allocator.deallocate(m_data);
			m_data = newData;
			m_capacity = capacity;
		}
	}

	const T& operator[](int index) const
	{
		ASSERT(index >= 0 && index < m_size);
		return m_data[index];
	}
	T& operator[](int index)
	{
		ASSERT(index >= 0 && index < m_size);
		return m_data[index];
	}
	int size() const { return m_size; }
	int capacity() const { return m_capacity; }

private:
	void grow()
	{
		int newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
		T* new_data = (T*)m_allocator.allocate(newCapacity * sizeof(T));
		memcpy(new_data, m_data, sizeof(T) * m_size);
		m_allocator.deallocate(m_data);
		m_data = new_data;
		m_capacity = newCapacity;
	}

	void callDestructors(T* begin, T* end)
	{
		for (; begin < end; ++begin)
		{
			begin->~T();
		}
	}

private:
	IAllocator& m_allocator;
	int m_capacity;
	int m_size;
	T* m_data;
};


template <typename T> class Array<T, true>
{
public:
	explicit Array(IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
	}

	explicit Array(const Array& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
		*this = rhs;
	}

	void operator=(const Array& rhs)
	{
		if (this != &rhs)
		{
			m_allocator.deallocate(m_data);
			m_data = (T*)m_allocator.allocate(rhs.m_capacity * sizeof(T));
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			::memmove(m_data, rhs.m_data, sizeof(T) * m_size);
		}
	}


	~Array() { m_allocator.deallocate(m_data); }


	T* begin() const { return m_data; }


	T* end() const { return &m_data[m_size]; }


	void swap(Array<T, true>& rhs)
	{
		ASSERT(&rhs.m_allocator == &m_allocator);

		int i = rhs.m_capacity;
		rhs.m_capacity = m_capacity;
		m_capacity = i;

		i = m_size;
		m_size = rhs.m_size;
		rhs.m_size = i;

		T* p = rhs.m_data;
		rhs.m_data = m_data;
		m_data = p;
	}

	int indexOf(const T& item) const
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				return i;
			}
		}
		return -1;
	}

	void eraseFast(int index)
	{
		if (index >= 0 && index < m_size)
		{
			if (index != m_size - 1)
			{
				memmove(m_data + index, m_data + m_size - 1, sizeof(T));
			}
			--m_size;
		}
	}

	void eraseItemFast(const T& item)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				eraseFast(i);
				return;
			}
		}
	}

	void erase(int index)
	{
		if (index >= 0 && index < m_size)
		{
			memmove(m_data + index,
					m_data + index + 1,
					sizeof(T) * (m_size - index - 1));
			--m_size;
		}
	}


	void eraseItem(const T& item)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				erase(i);
				return;
			}
		}
	}


	void insert(int index, const T& value)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		memmove(
			m_data + index + 1, m_data + index, sizeof(T) * (m_size - index));
		m_data[index] = value;
		++m_size;
	}


	void push(const T& value)
	{
		int size = m_size;
		if (size == m_capacity)
		{
			grow();
		}
		m_data[size] = value;
		++size;
		m_size = size;
	}

	template <typename... Params> T& emplace(Params&&... params)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		new ((char*)(m_data + m_size)) T(std::forward<Params>(params)...);
		++m_size;
		return m_data[m_size - 1];
	}

	bool empty() const { return m_size == 0; }

	void clear() { m_size = 0; }

	T& pushEmpty()
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		++m_size;
		return m_data[m_size - 1];
	}


	const T& back() const { return m_data[m_size - 1]; }


	T& back()
	{
		ASSERT(m_size > 0);
		return m_data[m_size - 1];
	}


	void pop()
	{
		if (m_size > 0)
		{
			--m_size;
		}
	}

	void resize(int size)
	{
		if (size > m_capacity)
		{
			reserve(size);
		}
		m_size = size;
	}

	void reserve(int capacity)
	{
		if (capacity > m_capacity)
		{
			T* newData = (T*)m_allocator.allocate(capacity * sizeof(T));
			memcpy(newData, m_data, sizeof(T) * m_size);
			m_allocator.deallocate(m_data);
			m_data = newData;
			m_capacity = capacity;
		}
	}

	const T& operator[](int index) const
	{
		ASSERT(index >= 0 && index < m_size);
		return m_data[index];
	}
	T& operator[](int index)
	{
		ASSERT(index >= 0 && index < m_size);
		return m_data[index];
	}
	int size() const { return m_size; }
	int capacity() const { return m_capacity; }

private:
	void grow()
	{
		int newCapacity = m_capacity == 0 ? 4 : m_capacity * 2;
		T* new_data = (T*)m_allocator.allocate(newCapacity * sizeof(T));
		memcpy(new_data, m_data, sizeof(T) * m_size);
		m_allocator.deallocate(m_data);
		m_data = new_data;
		m_capacity = newCapacity;
	}

private:
	IAllocator& m_allocator;
	int m_capacity;
	int m_size;
	T* m_data;
};


} // ~namespace Lumix

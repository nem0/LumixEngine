#pragma once


#include "engine/core/iallocator.h"
#include "engine/core/type_traits.h"

namespace Lumix
{

template <class T> inline void myswap(T &lhs, T &rhs)
{
	T tmp = mymove(lhs);
	lhs = mymove(rhs);
	rhs = mymove(tmp);
}

template <typename T> class Array
{
public:
	explicit Array(IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
	}

	Array(const Array& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
		*this = rhs;
	}

	Array(Array&& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_data = rhs.m_data;
		m_capacity = rhs.m_capacity;
		m_size = rhs.m_size;

		rhs.m_data = nullptr;
		rhs.m_capacity = 0;
		rhs.m_size = 0;
	}


	T* begin() const { return m_data; }


	T* end() const { return m_data + m_size; }


	void swap(Array<T>& rhs)
	{
		ASSERT(&rhs.m_allocator == &m_allocator);

		using Lumix::myswap;

		myswap(rhs.m_capacity, m_capacity);
		myswap(rhs.m_size, m_size);
		myswap(rhs.m_data, m_data);
	}


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
			Array(rhs).swap(*this);
		}
	}

	void operator=(Array&& rhs)
	{
		if (this != &rhs)
		{
			Array(mymove(rhs)).swap(*this);
		}
	}

	~Array()
	{
		callDestructors(m_data, m_data + m_size);
		m_allocator.deallocate(m_data);
	}

	template <typename R>
	int indexOf(R item) const
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
			if (index != m_size - 1)
			{
				using Lumix::myswap;
				myswap(m_data[index], m_data[m_size - 1]);
			}
			m_data[m_size - 1].~T();
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
		if (index == m_size)
		{
			push(value);
			return;
		}
		makePlaceAt(index);
		m_data[index] = value;
	}


	void erase(int index)
	{
		if (index < 0 && index >= m_size)
		{
			return;
		}

		for (int i = index; i < m_size - 1; ++i)
		{
			m_data[i] = mymove(m_data[i + 1]);
		}
		m_data[m_size - 1].~T();
		--m_size;
	}

	void push(const T& value)
	{
		ensureCapacity();
		new (NewPlaceholder(), (char*)(m_data + m_size)) T(value);
		++m_size;
	}

	void push(T&& value)
	{
		ensureCapacity();
		new (NewPlaceholder(), (char*)(m_data + m_size)) T(mymove(value));
		++m_size;
	}

	template <typename... Params> T& emplace(Params&&... params)
	{
		ensureCapacity();
		new (NewPlaceholder(), (char*)(m_data + m_size)) T(myforward<Params>(params)...);
		++m_size;
		return m_data[m_size - 1];
	}

	template <typename... Params> T& emplaceAt(int idx, Params&&... params)
	{
		if (idx == m_size)
		{
			return emplace(myforward<Params>(params)...);
		}
		makePlaceAt(idx);
		T tmp(myforward<Params>(params)...);
		m_data[idx] = mymove(tmp);
		return m_data[idx];
	}

	bool empty() const { return m_size == 0; }

	void clear()
	{
		callDestructors(m_data, m_data + m_size);
		m_size = 0;
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
			new (NewPlaceholder(), (char*)(m_data + i)) T();
		}
		if (size < m_size)
		{
			callDestructors(m_data + size, m_data + m_size);
		}
		m_size = size;
	}

	void reserve(int capacity)
	{
		if (capacity > m_capacity)
		{
			growTo(capacity);
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
		growTo(newCapacity);
	}

	void growFromEmpty(int newCapacity)
	{
		ASSERT(m_size == 0);
		ASSERT(m_capacity == 0);
		ASSERT(m_data == nullptr);
		m_data = (T*)m_allocator.allocate(newCapacity * sizeof(T));
		m_capacity = newCapacity;
	}

	void growTo(int newCapacity)
	{
		Array temp(m_allocator);
		temp.growFromEmpty(newCapacity);
		for (int i = 0; i < m_size; ++i)
		{
			temp.push(mymove(m_data[i]));
		}
		swap(temp);
	}

	void makePlaceAt(int idx)
	{
		ASSERT(idx >= 0 && idx < m_size);
		ensureCapacity();
		new (NewPlaceholder(), (char *)(m_data + m_size)) T(mymove(m_data[m_size-1]));
		for (int i = m_size - 1; i - 1 >= idx; --i)
		{
			m_data[i] = mymove(m_data[i - 1]);
		}
		++m_size;
	}

	void ensureCapacity()
	{
		if (m_size == m_capacity)
		{
			grow();
		}
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


} // ~namespace Lumix

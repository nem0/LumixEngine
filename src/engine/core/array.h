#pragma once


#include "engine/core/iallocator.h"


namespace Lumix
{


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


	void swap(Array<T>& rhs)
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
				new (Lumix::NewPlaceholder(), (char*)(m_data + i)) T(rhs.m_data[i]);
			}
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
			m_data[index].~T();
			if (index != m_size - 1)
			{
				moveMemory(m_data + index, m_data + m_size - 1, sizeof(T));
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
		moveMemory(m_data + index + 1, m_data + index, sizeof(T) * (m_size - index));
		new (NewPlaceholder(), &m_data[index]) T(value);
		++m_size;
	}


	void erase(int index)
	{
		if (index >= 0 && index < m_size)
		{
			m_data[index].~T();
			if (index < m_size - 1)
			{
				moveMemory(m_data + index, m_data + index + 1, sizeof(T) * (m_size - index - 1));
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
		new (NewPlaceholder(), (char*)(m_data + size)) T(value);
		++size;
		m_size = size;
	}


	template <class _Ty> struct remove_reference
	{ // remove rvalue reference
		typedef _Ty type;
	};


	template <class _Ty> struct remove_reference<_Ty&>
	{ // remove rvalue reference
		typedef _Ty type;
	};

	template <class _Ty> struct remove_reference<_Ty&&>
	{ // remove rvalue reference
		typedef _Ty type;
	};

	template <class _Ty> inline _Ty&& myforward(typename remove_reference<_Ty>::type& _Arg)
	{
		return (static_cast<_Ty&&>(_Arg));
	}

	template <typename... Params> T& emplace(Params&&... params)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		new (NewPlaceholder(), (char*)(m_data + m_size)) T(myforward<Params>(params)...);
		++m_size;
		return m_data[m_size - 1];
	}

	template <typename... Params> T& emplaceAt(int idx, Params&&... params)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		for (int i = m_size - 1; i >= idx; --i)
		{
			copyMemory(&m_data[i + 1], &m_data[i], sizeof(m_data[i]));
		}
		new (NewPlaceholder(), (char*)(m_data + idx)) T(myforward<Params>(params)...);
		++m_size;
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
		callDestructors(m_data + size, m_data + m_size);
		m_size = size;
	}

	void moveMemory(void* dest, const void* src, size_t count)
	{
		if(dest == src) return;

		if(dest < src || dest >= (uint8*)src + count)
		{
			uint8* dest8 = (uint8*)dest;
			const uint8* src8 = (const uint8*)src;

			const uint8* src_end = src8 + count;
			while(src8 != src_end)
			{
				*dest8 = *src8;
				++src8;
				++dest8;
			}
		}
		else
		{
			uint8* dest8 = (uint8*)dest + count - 1;
			const uint8* src8 = (const uint8*)src + count - 1;

			while(src8 >= src)
			{
				*dest8 = *src8;
				--src8;
				--dest8;
			}
		}
	}

	void copyMemory(void* dest, const void* src, size_t count)
	{
		uint8* dest8 = (uint8*)dest;
		const uint8* src8 = (const uint8*)src;

		const uint8* src_end = src8 + count;
		while(src8 != src_end)
		{
			*dest8 = *src8;
			++src8;
			++dest8;
		}
	}

	void reserve(int capacity)
	{
		if (capacity > m_capacity)
		{
			T* newData = (T*)m_allocator.allocate(capacity * sizeof(T));
			copyMemory(newData, m_data, sizeof(T) * m_size);
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
		copyMemory(new_data, m_data, sizeof(T) * m_size);
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


} // ~namespace Lumix

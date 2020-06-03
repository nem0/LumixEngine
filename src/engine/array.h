#pragma once


#include "engine/allocator.h"
#include "engine/crt.h"


namespace Lumix
{


template <typename T> struct Array
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


	Array(Array&& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_data = nullptr;
		m_capacity = 0;
		m_size = 0;
		
		swap(rhs);
	}


	T* begin() const { return m_data; }


	T* end() const { return m_data ? m_data + m_size : nullptr; }


	operator Span<T>() const { return Span(begin(), end()); }
	operator Span<const T>() const { return Span(begin(), end()); }


	void swap(Array<T>& rhs)
	{
		ASSERT(&rhs.m_allocator == &m_allocator);

		u32 i = rhs.m_capacity;
		rhs.m_capacity = m_capacity;
		m_capacity = i;

		i = m_size;
		m_size = rhs.m_size;
		rhs.m_size = i;

		T* p = rhs.m_data;
		rhs.m_data = m_data;
		m_data = p;
	}


	template <typename Comparator>
	void removeDuplicates(Comparator equals)
	{
		if (m_size == 0) return;
		for (u32 i = 0; i < m_size - 1; ++i)
		{
			for (u32 j = i + 1; j < m_size; ++j)
			{
				if (equals(m_data[i], m_data[j]))
				{
					swapAndPop(j);
					--j;
				}
			}
		}
	}


	void removeDuplicates()
	{
		if (m_size == 0) return;
		for (u32 i = 0; i < m_size - 1; ++i)
		{
			for (u32 j = i + 1; j < m_size; ++j)
			{
				if (m_data[i] == m_data[j])
				{
					swapAndPop(j);
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
			m_allocator.deallocate_aligned(m_data);
			m_data = (T*)m_allocator.allocate_aligned(rhs.m_capacity * sizeof(T), alignof(T));
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			for (u32 i = 0; i < m_size; ++i)
			{
				new (NewPlaceholder(), (char*)(m_data + i)) T(rhs.m_data[i]);
			}
		}
	}


	void operator=(Array&& rhs)
	{
		ASSERT(&m_allocator == &rhs.m_allocator);
		if (this != &rhs)
		{
			callDestructors(m_data, m_data + m_size);
			m_allocator.deallocate_aligned(m_data);
			m_data = rhs.m_data;
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			rhs.m_data = nullptr;
			rhs.m_capacity = 0;
			rhs.m_size = 0;
		}
	}


	void free()
	{
		clear();
		m_allocator.deallocate_aligned(m_data);
		m_capacity = 0;
		m_data = nullptr;
	}


	~Array()
	{
		callDestructors(m_data, m_data + m_size);
		m_allocator.deallocate_aligned(m_data);
	}

	template <typename F>
	int find(F predicate) const
	{
		for (u32 i = 0; i < m_size; ++i)
		{
			if (predicate(m_data[i]))
			{
				return i;
			}
		}
		return -1;
	}

	template <typename R>
	int indexOf(R item) const
	{
		for (u32 i = 0; i < m_size; ++i)
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
		for (u32 i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				return i;
			}
		}
		return -1;
	}

	template <typename F>
	void eraseItems(F predicate)
	{
		for (u32 i = m_size - 1; i != 0xffFFffFF; --i)
		{
			if (predicate(m_data[i]))
			{
				erase(i);
			}
		}
	}

	void swapAndPopItem(const T& item)
	{
		for (u32 i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				swapAndPop(i);
				return;
			}
		}
	}

	void swapAndPop(u32 index)
	{
		if (index >= 0 && index < m_size)
		{
			m_data[index].~T();
			if (index != m_size - 1)
			{
				memmove(m_data + index, m_data + m_size - 1, sizeof(T));
			}
			--m_size;
		}
	}

	void eraseItem(const T& item)
	{
		for (u32 i = 0; i < m_size; ++i)
		{
			if (m_data[i] == item)
			{
				erase(i);
				return;
			}
		}
	}


	void insert(u32 index, const T& value)
	{
		if (m_size == m_capacity)
		{
			grow();
		}
		memmove(m_data + index + 1, m_data + index, sizeof(T) * (m_size - index));
		new (NewPlaceholder(), &m_data[index]) T(value);
		++m_size;
	}


	void erase(u32 index)
	{
		if (index < m_size)
		{
			m_data[index].~T();
			if (index < m_size - 1)
			{
				memcpy(m_data + index, m_data + index + 1, sizeof(T) * (m_size - index - 1));
			}
			--m_size;
		}
	}

	void push(const T& value)
	{
		u32 size = m_size;
		if (size == m_capacity)
		{
			grow();
		}
		new (NewPlaceholder(), (char*)(m_data + size)) T(value);
		++size;
		m_size = size;
	}


	template <typename _Ty> struct remove_reference { typedef _Ty type; };
	template <typename _Ty> struct remove_reference<_Ty&> { typedef _Ty type; };
	template <typename _Ty> struct remove_reference<_Ty&&> { typedef _Ty type; };

	template <typename _Ty> _Ty&& myforward(typename remove_reference<_Ty>::type& _Arg)
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

	template <typename... Params> T& emplaceAt(u32 idx, Params&&... params)
	{
		if (m_size == m_capacity) grow();
		
		memmove(&m_data[idx + 1], &m_data[idx], sizeof(m_data[idx]) * (m_size - idx));
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


	T& back() { ASSERT(m_size > 0); return m_data[m_size - 1]; }


	void pop()
	{
		if (m_size > 0)
		{
			m_data[m_size - 1].~T();
			--m_size;
		}
	}

	void resize(u32 size)
	{
		if (size > m_capacity)
		{
			reserve(size);
		}
		for (u32 i = m_size; i < size; ++i)
		{
			new (NewPlaceholder(), (char*)(m_data + i)) T;
		}
		callDestructors(m_data + size, m_data + m_size);
		m_size = size;
	}

	void reserve(u32 capacity)
	{
		if (capacity > m_capacity)
		{
			T* newData = (T*)m_allocator.allocate_aligned(capacity * sizeof(T), alignof(T));
			memcpy(newData, m_data, sizeof(T) * m_size);
			m_allocator.deallocate_aligned(m_data);
			m_data = newData;
			m_capacity = capacity;
		}
	}

	const T& operator[](u32 index) const
	{
		ASSERT(index < m_size);
		return m_data[index];
	}

	T& operator[](u32 index)
	{
		ASSERT(index < m_size);
		return m_data[index];
	}

	u32 byte_size() const { return m_size * sizeof(T); }
	int size() const { return m_size; }
	
	// can be used instead of resize when resize won't compile because of unsuitable constructor
	void shrink(u32 new_size) { 
		ASSERT(new_size <= m_size);
		for (u32 i = new_size; i < m_size; ++i) {
			m_data[i].~T();
		}
		m_size = new_size;
	}

	u32 capacity() const { return m_capacity; }

private:
	void grow()
	{
		u32 new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
		m_data = (T*)m_allocator.reallocate_aligned(m_data, new_capacity * sizeof(T), alignof(T));
		m_capacity = new_capacity;
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
	u32 m_capacity;
	u32 m_size;
	T* m_data;
};


} // namespace Lumix

#pragma once

#include "foundation/allocator.h"
#include "foundation/crt.h"

namespace Lumix {

template <typename T> struct Array {
	explicit Array(IAllocator& allocator)
		: m_allocator(allocator)
		, m_data(nullptr)
		, m_capacity(0)
		, m_size(0) {}

	Array(Array&& rhs)
		: m_allocator(rhs.m_allocator)
		, m_data(nullptr)
		, m_capacity(0)
		, m_size(0) {
		swap(rhs);
	}

	Array(const Array& rhs) = delete;
	void operator=(const Array& rhs) = delete;

	Array<T>&& move() { return static_cast<Array<T>&&>(*this); }

	T* data() const { return m_data; }
	T* begin() const { return m_data; }
	T* end() const { return m_data ? m_data + m_size : nullptr; }

	const T& last() const { ASSERT(m_size > 0); return m_data[m_size - 1]; }
	T& last() { ASSERT(m_size > 0); return m_data[m_size - 1]; }

	template <typename T2> operator Span<T2*>() const { return Span(reinterpret_cast<T2**>(begin()), reinterpret_cast<T2**>(end())); }
	operator Span<T>() const { return Span(begin(), end()); }
	operator Span<const T>() const { return Span(begin(), end()); }

	void swap(Array<T>& rhs) {
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

	template <typename Comparator> void removeDuplicates(Comparator equals) {
		if (m_size == 0) return;
		for (u32 i = 0; i < m_size - 1; ++i) {
			for (u32 j = i + 1; j < m_size; ++j) {
				if (equals(m_data[i], m_data[j])) {
					swapAndPop(j);
					--j;
				}
			}
		}
	}

	void removeDuplicates() {
		if (m_size == 0) return;
		for (u32 i = 0; i < m_size - 1; ++i) {
			for (u32 j = i + 1; j < m_size; ++j) {
				if (m_data[i] == m_data[j]) {
					swapAndPop(j);
					--j;
				}
			}
		}
	}

	void copyTo(Array<T>& dst) const {
		if (m_size == 0) {
			dst.clear();
			return;
		}
		if constexpr (__is_trivially_copyable(T)) {
			dst.resize(m_size);
			memcpy(dst.begin(), begin(), sizeof(T) * m_size);
			return;
		}
		else {
			dst.clear();
			dst.reserve(m_size);
			for (const T& v : *this) {
				dst.push(v);
			}
		}
	}

	void operator=(Array&& rhs) {
		ASSERT(&m_allocator == &rhs.m_allocator);
		if (this != &rhs) {
			callDestructors(m_data, m_data + m_size);
			m_allocator.deallocate(m_data);
			m_data = rhs.m_data;
			m_capacity = rhs.m_capacity;
			m_size = rhs.m_size;
			rhs.m_data = nullptr;
			rhs.m_capacity = 0;
			rhs.m_size = 0;
		}
	}

	~Array() {
		if (!m_data) return;
		callDestructors(m_data, m_data + m_size);
		m_allocator.deallocate(m_data);
	}

	template <typename F> int find(F predicate) const {
		for (u32 i = 0; i < m_size; ++i) {
			if (predicate(m_data[i])) {
				return i;
			}
		}
		return -1;
	}

	template <typename R> int indexOf(const R& item) const {
		for (u32 i = 0; i < m_size; ++i) {
			if (m_data[i] == item) {
				return i;
			}
		}
		return -1;
	}

	int indexOf(const T& item) const {
		for (u32 i = 0; i < m_size; ++i) {
			if (m_data[i] == item) {
				return i;
			}
		}
		return -1;
	}

	template <typename F> void eraseItems(F predicate) {
		for (u32 i = m_size - 1; i != 0xffFFffFF; --i) {
			if (predicate(m_data[i])) {
				erase(i);
			}
		}
	}

	void eraseRange(u32 from, u32 count) {
		ASSERT(from + count <= m_size);
		ASSERT(count > 0);

		if constexpr (__is_trivially_copyable(T)) {
			memcpy(m_data + from, m_data + from + count, (m_size - from - count) * sizeof(T));
		}
		else {
			for (u32 i = from; i < m_size - count; ++i) {
				m_data[i].~T();	
				new (NewPlaceholder(), m_data + i) T(static_cast<T&&>(m_data[i + count]));
			}
			m_data[m_size - 1].~T();
		}
		m_size -= count;
	}

	void swapAndPopItem(const T& item) {
		for (u32 i = 0; i < m_size; ++i) {
			if (m_data[i] == item) {
				swapAndPop(i);
				return;
			}
		}
	}

	void swapAndPop(u32 index) {
		ASSERT(index < m_size);

		if constexpr (__is_trivially_copyable(T)) {
			memmove(m_data + index, m_data + m_size - 1, sizeof(T));
		} else {
			m_data[index].~T();
			if (index != m_size - 1) {
				new (NewPlaceholder(), m_data + index) T(static_cast<T&&>(m_data[m_size - 1]));
				m_data[m_size - 1].~T();
			}
		}
		--m_size;
	}

	void eraseItem(const T& item) {
		for (u32 i = 0; i < m_size; ++i) {
			if (m_data[i] == item) {
				erase(i);
				return;
			}
		}
	}

	void erase(u32 index) {
		ASSERT (index < m_size);
		m_data[index].~T();
		if (index < m_size - 1) {
			if constexpr (__is_trivially_copyable(T)) {
				memmove(m_data + index, m_data + index + 1, sizeof(T) * (m_size - index - 1));
			} else {
				for (u32 i = index; i < m_size - 1; ++i) {
					new (NewPlaceholder(), &m_data[i]) T(static_cast<T&&>(m_data[i + 1]));
					m_data[i + 1].~T();
				}
			}
		}
		--m_size;
	}

	void push(T&& value) {
		u32 size = m_size;
		if (size == m_capacity) grow();
		new (NewPlaceholder(), (char*)(m_data + size)) T(static_cast<T&&>(value));
		++size;
		m_size = size;
	}

	void push(const T& value) {
		u32 size = m_size;
		if (size == m_capacity) grow();
		new (NewPlaceholder(), (char*)(m_data + size)) T(value);
		++size;
		m_size = size;
	}

	template <typename... Params> T& emplace(Params&&... params) {
		if (m_size == m_capacity) grow();
		new (NewPlaceholder(), (char*)(m_data + m_size)) T(static_cast<Params&&>(params)...);
		++m_size;
		return m_data[m_size - 1];
	}

	static void moveRange(T* dst, T* src, u32 count) {
		ASSERT(dst > src || dst + count < src);
		if constexpr (__is_trivially_copyable(T)) {
			memcpy(dst, src, sizeof(T) * count);
		}
		else {
			for (u32 i = count - 1; i < count; --i) {
				new (NewPlaceholder(), dst + i) T(static_cast<T&&>(src[i]));
				src[i].~T();
			}
		}
	}

	template <typename... Params> T& emplaceAt(u32 idx, Params&&... params) {
		if constexpr (__is_trivially_copyable(T)) {
			if (m_size == m_capacity) grow();
			memmove(&m_data[idx + 1], &m_data[idx], sizeof(m_data[idx]) * (m_size - idx));
			new (NewPlaceholder(), (char*)(m_data + idx)) T(static_cast<Params&&>(params)...);
		} else {
			if (m_size == m_capacity) {
				u32 new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
				T* old_data = m_data;
				m_data = (T*)m_allocator.allocate(new_capacity * sizeof(T), alignof(T));
				moveRange(m_data, old_data, idx);
				moveRange(m_data + idx + 1, old_data + idx, m_size - idx);
				m_allocator.deallocate(old_data);
				m_capacity = new_capacity;
			} else {
				moveRange(m_data + idx + 1, m_data + idx, m_size - idx);
			}
			new (NewPlaceholder(), m_data + idx) T(static_cast<Params&&>(params)...);
		}
		++m_size;
		return m_data[idx];
	}

	void insert(u32 index, const T& value) { emplaceAt(index, value); }

	void insert(u32 index, T&& value) { emplaceAt(index, static_cast<T&&>(value)); }

	bool empty() const { return m_size == 0; }

	void clear() {
		callDestructors(m_data, m_data + m_size);
		m_size = 0;
	}

	const T& back() const { return m_data[m_size - 1]; }

	T& back() {
		ASSERT(m_size > 0);
		return m_data[m_size - 1];
	}

	void pop() {
		if (m_size > 0) {
			m_data[m_size - 1].~T();
			--m_size;
		}
	}

	void resize(u32 size) {
		if (size > m_capacity) {
			reserve(size);
		}
		for (u32 i = m_size; i < size; ++i) {
			new (NewPlaceholder(), (char*)(m_data + i)) T;
		}
		callDestructors(m_data + size, m_data + m_size);
		m_size = size;
	}

	void reserve(u32 capacity) {
		if (capacity > m_capacity) {
			T* new_data = (T*)m_allocator.allocate(capacity * sizeof(T), alignof(T));
			moveRange(new_data, m_data, m_size);
			m_allocator.deallocate(m_data);
			m_data = new_data;
			m_capacity = capacity;
		}
	}

	const T& operator[](u32 index) const {
		ASSERT(index < m_size);
		return m_data[index];
	}

	T& operator[](u32 index) {
		ASSERT(index < m_size);
		return m_data[index];
	}

	u32 byte_size() const { return m_size * sizeof(T); }
	i32 size() const { return m_size; }

	// can be used instead of resize when resize won't compile because of unsuitable constructor
	void shrink(u32 new_size) {
		ASSERT(new_size <= m_size);
		for (u32 i = new_size; i < m_size; ++i) {
			m_data[i].~T();
		}
		m_size = new_size;
	}

	u32 capacity() const { return m_capacity; }
	IAllocator& getAllocator() const { return m_allocator; }

protected:
	void grow() { reserve(m_capacity < 4 ? 4 : m_capacity + m_capacity / 2); }

	void callDestructors(T* begin, T* end) {
		for (; begin < end; ++begin) {
			begin->~T();
		}
	}

	IAllocator& m_allocator;
	u32 m_capacity;
	u32 m_size;
	T* m_data;
};

} // namespace Lumix

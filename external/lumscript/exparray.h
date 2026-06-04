#pragma once

#include "capi.h"

void* operator new(decltype(sizeof(0)), void* ptr) noexcept;
void operator delete(void*, void*) noexcept;

// allocates using arena
// never deallocates on its own, so deallocation should be done in layer above
// Arena-backed exponential array. Bins grow by powers of two:
// 4, 8, 16, 32, ...
template <typename T>
struct ExpArray {
	explicit ExpArray(ls_arena& arena) : arena(arena) {
		for (T*& bin : bins) bin = nullptr;
	}

	~ExpArray() {
		for (i32 i = 0; i < m_size; ++i) {
			(*this)[i].~T();
		}
	}

	ExpArray(const ExpArray&) = delete;
	ExpArray& operator=(const ExpArray&) = delete;

	void push(const T& value) {
		(void)emplace_back(value);
	}

	void push(T&& value) {
		(void)emplace_back((T&&)value);
	}

	void push_back(const T& value) {
		(void)emplace_back(value);
	}

	void push_back(T&& value) {
		(void)emplace_back((T&&)value);
	}

	template <typename... Args>
	T& emplace_back(Args&&... args) {
		ensureCapacity(m_size + 1);
		const i32 outter_index = binIndex(m_size);
		const i32 inner_index = innerIndex(m_size);
		T* bin = bins[outter_index];
		ASSERT(bin);
		T* out = bin + inner_index;
		::new ((void*)out) T((Args&&)args...);
		++m_size;
		return *out;
	}

	void resize(i32 new_size) {
		if (new_size < m_size) {
			for (i32 i = new_size; i < m_size; ++i) {
				(*this)[i].~T();
			}
			m_size = new_size;
			return;
		}
		ensureCapacity(new_size);
		while (m_size < new_size) {
			const i32 outter_index = binIndex(m_size);
			const i32 inner_index = innerIndex(m_size);
			T* bin = bins[outter_index];
			ASSERT(bin);
			::new ((void*)(bin + inner_index)) T();
			++m_size;
		}
	}

	void resize(i32 new_size, const T& value) {
		if (new_size < m_size) {
			for (i32 i = new_size; i < m_size; ++i) {
				(*this)[i].~T();
			}
			m_size = new_size;
			return;
		}
		ensureCapacity(new_size);
		while (m_size < new_size) {
			const i32 outter_index = binIndex(m_size);
			const i32 inner_index = innerIndex(m_size);
			T* bin = bins[outter_index];
			ASSERT(bin);
			::new ((void*)(bin + inner_index)) T(value);
			++m_size;
		}
	}

	void clear() {
		resize(0);
	}

	void pop_back() {
		ASSERT(m_size > 0);
		resize(m_size - 1);
	}

	T& back() {
		ASSERT(m_size > 0);
		return (*this)[m_size - 1];
	}

	const T& back() const {
		ASSERT(m_size > 0);
		return (*this)[m_size - 1];
	}

	struct iterator {
		ExpArray* array = nullptr;
		i32 index = 0;

		T& operator*() const { return (*array)[index]; }
		iterator& operator++() {
			++index;
			return *this;
		}
		bool operator!=(const iterator& rhs) const {
			return array != rhs.array || index != rhs.index;
		}
	};

	struct const_iterator {
		const ExpArray* array = nullptr;
		i32 index = 0;

		const T& operator*() const { return (*array)[index]; }
		const_iterator& operator++() {
			++index;
			return *this;
		}
		bool operator!=(const const_iterator& rhs) const {
			return array != rhs.array || index != rhs.index;
		}
	};

	iterator begin() { return iterator{this, 0}; }
	iterator end() { return iterator{this, m_size}; }
	const_iterator begin() const { return const_iterator{this, 0}; }
	const_iterator end() const { return const_iterator{this, m_size}; }
	const_iterator cbegin() const { return begin(); }
	const_iterator cend() const { return end(); }

	i32 size() const { return m_size; }
	bool empty() const { return m_size == 0; }

	i32 indexOf(const T* ptr) const {
		for (i32 i = 0; i < m_size; ++i) {
			if (&(*this)[i] == ptr) return i;
		}
		return -1;
	}

	const T& operator[](i32 index) const {
		return const_cast<ExpArray*>(this)->operator[](index);
	}

	T& operator[](i32 index) {
		ASSERT(index >= 0 && index < m_size);
		const i32 outter_index = binIndex(index);
		const i32 inner_index = innerIndex(index);
		return bins[outter_index][inner_index];
	}

	void allocateNextBin() {
		ASSERT(m_num_bins < (i32)(sizeof(bins) / sizeof(bins[0])));
		if (m_num_bins >= (i32)(sizeof(bins) / sizeof(bins[0]))) {
			return;
		}
		const i32 bin_size = 4 << m_num_bins;
		bins[m_num_bins] = static_cast<T*>(arena.allocate(arena.user_data, sizeof(T) * (size_t)bin_size, alignof(T)));
		ASSERT(bins[m_num_bins]);
		m_capacity += bin_size;
		++m_num_bins;
	}

	void ensureCapacity(i32 desired_size) {
		while (desired_size > m_capacity) {
			const i32 old_capacity = m_capacity;
			allocateNextBin();
			if (m_capacity == old_capacity) {
				break;
			}
		}
		ASSERT(desired_size <= m_capacity);
	}

private:
	static i32 binIndex(i32 index) {
		ASSERT(index >= 0);
		unsigned long msb = 0;
		ASSERT(_BitScanReverse(&msb, (unsigned long)index + 4));
		return (i32)msb - 2;
	}

	static i32 innerIndex(i32 index) {
		unsigned long msb = 0;
		ASSERT(_BitScanReverse(&msb, (unsigned long)index + 4));
		return index + 4 - (1 << msb);
	}

	T* bins[20];
	ls_arena& arena;
	i32 m_size = 0;
	i32 m_num_bins = 0;
	i32 m_capacity = 0;
};

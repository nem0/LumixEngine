#pragma once

#include "engine/allocator.h"

namespace Lumix {

template <typename T, u32 CAPACITY>
struct FixedArray {
	~FixedArray() {
		for (u32 i = 0; i < m_size; ++i) {
			operator[](i).~T();
		}
	}

	template <typename... Args>
	T& emplace(Args&&... args) {
		ASSERT(!is_full());
		T* v = new (NewPlaceholder(), m_mem + sizeof(T) * m_size) T(static_cast<Args&&>(args)...);
		++m_size;
		return *v;
	}

	constexpr u32 capacity() { return CAPACITY; }
	u32 size() const { return m_size; }
	bool is_full() const { return m_size == CAPACITY; }
	T& operator[](u32 idx) { ASSERT(idx < m_size); return ((T*)m_mem)[idx]; }
	const T& operator[](u32 idx) const { ASSERT(idx < m_size); return ((const T*)m_mem)[idx]; }

	void pop() {
		ASSERT(m_size > 0);
		operator[](m_size - 1).~T();
		--m_size;
	}

	const T* begin() const { return (T*)m_mem; }
	const T* end() const { return ((T*)m_mem) + m_size; }

	const T& last() const { return operator[](m_size - 1); }

	void push(const T& v) {
		ASSERT(!is_full());
		new (NewPlaceholder(), m_mem + sizeof(T) * m_size) T(v);
		++m_size;
	}

private:
	u32 m_size = 0;
	alignas(T) u8 m_mem[sizeof(T) * CAPACITY];
};

} // namespace Lumix
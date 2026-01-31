#pragma once

#include "core/allocator.h"

namespace black {

// one allocation from local memory backing (m_mem), use fallback allocator otherwise
// use case: StackArray<T, N> to allocate on stack
template <u32 CAPACITY, u32 ALIGN = 8>
struct StackAllocator final : IAllocator {
	explicit StackAllocator(IAllocator& fallback) : m_fallback(fallback) {}
	~StackAllocator() { ASSERT(!m_allocated); }

	void* allocate(size_t size, size_t align) override {
		ASSERT(align <= ALIGN);
		if (!m_allocated && size <= CAPACITY) {
			m_allocated = true;
			return m_mem;
		}
		return m_fallback.allocate(size, align);
	}

	void deallocate(void* ptr) override {
		if (!ptr) return;
		if (ptr == m_mem) {
			m_allocated = false;
			return;
		}
		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		m_fallback.deallocate(ptr);
	}

	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override {
		ASSERT(align <= ALIGN);
		if (!ptr) return allocate(new_size, align);
		if (ptr == m_mem) {
			ASSERT(m_allocated);
			if (new_size <= CAPACITY) return m_mem;

			m_allocated = false;
			void* n = m_fallback.allocate(new_size, align);
			memcpy(n, m_mem, CAPACITY);
			return n;
		}

		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		if (new_size > CAPACITY) return m_fallback.reallocate(ptr, new_size, old_size, align);
		memcpy(m_mem, ptr, new_size);
		m_allocated = true;
		m_fallback.deallocate(ptr);
		return m_mem;
	}

private:
	bool m_allocated = false;
	alignas(ALIGN) u8 m_mem[CAPACITY];
	IAllocator& m_fallback;
};

}
#pragma once

#include "allocator.h"
#include "sync.h"

namespace Lumix {

// use buckets for small allocations - relatively fast
// fallback to system allocator for big allocations
// use case: use this unless you really require something special
struct LUMIX_ENGINE_API DefaultAllocator final : IAllocator {
	struct Page;

	DefaultAllocator();
	~DefaultAllocator();

	void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t new_size, size_t old_size, size_t align) override;

	u8* m_small_allocations = nullptr;
	Page* m_free_lists[4];
	u32 m_page_count = 0;
	Mutex m_mutex;
};

// detects memory leaks, just by counting number of allocations - very fast
struct LUMIX_ENGINE_API BaseProxyAllocator final : IAllocator {
	explicit BaseProxyAllocator(IAllocator& source);
	~BaseProxyAllocator();

	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t new_size, size_t old_size, size_t align) override;
	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size) override;
	IAllocator& getSourceAllocator() { return m_source; }

private:
	IAllocator& m_source;
	volatile i32 m_allocation_count;
};

// allocations in a row one after another, deallocate everything at once
// use case: data for one frame
struct LUMIX_ENGINE_API LinearAllocator : IAllocator {
	LinearAllocator(u32 reserved);
	~LinearAllocator();

	void reset();
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t new_size, size_t old_size, size_t align) override;
	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size) override;

	u32 getCommited() const { return m_commited; }

private:
	u32 m_commited;
	u32 m_reserved;
	volatile i32 m_end;
	u8* m_mem;
	Mutex m_mutex;
};

// one allocation from local memory backing (m_mem), use fallback allocator otherwise
// use case: StackArray<T, N> to allocate on stack
template <u32 CAPACITY, u32 ALIGN = 8>
struct StackAllocator final : IAllocator {
	explicit StackAllocator(IAllocator& fallback) : m_fallback(fallback) {}
	~StackAllocator() { ASSERT(!m_allocated); }

	void* allocate_aligned(size_t size, size_t align) override {
		ASSERT(align <= ALIGN);
		if (!m_allocated && size <= CAPACITY) {
			m_allocated = true;
			return m_mem;
		}
		return m_fallback.allocate_aligned(size, align);
	}

	void deallocate_aligned(void* ptr) override {
		if (!ptr) return;
		if (ptr == m_mem) {
			m_allocated = false;
			return;
		}
		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		m_fallback.deallocate_aligned(ptr);
	}

	void* reallocate_aligned(void* ptr, size_t new_size, size_t old_size, size_t align) override {
		ASSERT(align <= ALIGN);
		if (!ptr) return allocate_aligned(new_size, align);
		if (ptr == m_mem) {
			ASSERT(m_allocated);
			if (new_size <= CAPACITY) return m_mem;
			
			m_allocated = false;
			void* n = m_fallback.allocate_aligned(new_size, align);
			memcpy(n, m_mem, CAPACITY);
			return n;
		}
		
		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		if (new_size > CAPACITY) return m_fallback.reallocate_aligned(ptr, new_size, old_size, align);
		memcpy(m_mem, ptr, new_size);
		m_allocated = true;
		m_fallback.deallocate_aligned(ptr);
		return m_mem;
	}

	void* allocate(size_t size) override { ASSERT(false); return nullptr; }
	void deallocate(void* ptr) override { ASSERT(false); }
	void* reallocate(void* ptr, size_t new_size, size_t old_size) override { ASSERT(false); return nullptr; }

private:
	bool m_allocated = false;
	alignas(ALIGN) u8 m_mem[CAPACITY];
	IAllocator& m_fallback;
};

} // namespace Lumix

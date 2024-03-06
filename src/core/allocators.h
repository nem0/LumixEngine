#pragma once

#include "allocator.h"
#include "atomic.h"
#include "crt.h"
#include "sync.h"

namespace Lumix {

// use buckets for small allocations - relatively fast
// fallback to system allocator for big allocations
// use case: use this unless you really require something special
struct LUMIX_CORE_API DefaultAllocator final : IAllocator {
	struct Page;

	DefaultAllocator();
	~DefaultAllocator();

	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;

	u8* m_small_allocations = nullptr;
	Page* m_free_lists[4];
	u32 m_page_count = 0;
	Mutex m_mutex;
};

// set active_allocator before calling its parent allocator, parent allocator can use the tag to e.g. group allocations
struct LUMIX_CORE_API TagAllocator final : IAllocator {
	TagAllocator(IAllocator& allocator, const char* tag_name);

	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;

	IAllocator* getParent() const override { return m_direct_parent; }
	bool isTagAllocator() const override { return true; }

	static TagAllocator* getActiveAllocator();

	IAllocator* m_direct_parent;
	// skip TagAllocator parents when actually allocating
	IAllocator* m_effective_allocator;
	const char* m_tag;
};

// detects memory leaks, just by counting number of allocations - very fast
struct LUMIX_CORE_API BaseProxyAllocator final : IAllocator {
	explicit BaseProxyAllocator(IAllocator& source);
	~BaseProxyAllocator();

	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;
	IAllocator& getSourceAllocator() { return m_source; }
	
	IAllocator* getParent() const override { return &m_source; }

private:
	IAllocator& m_source;
	AtomicI32 m_allocation_count;
};

// allocations in a row one after another, deallocate everything at once
// use case: data for one frame
struct LUMIX_CORE_API LinearAllocator : IAllocator {
	LinearAllocator(u32 reserved);
	~LinearAllocator();

	void reset();
	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;

	u32 getCommitedBytes() const { return m_commited_bytes; }
	static inline size_t getTotalCommitedBytes() { return g_total_commited_bytes; }

private:
	u32 m_commited_bytes = 0;
	u32 m_reserved;
	AtomicI32 m_end = 0;
	u8* m_mem;
	Mutex m_mutex;

	static inline AtomicI64 g_total_commited_bytes = 0;
};

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

// used for stuff that can't access engine's allocator, e.g. global objects constructed before engine such as logger
LUMIX_CORE_API IAllocator& getGlobalAllocator();

} // namespace Lumix

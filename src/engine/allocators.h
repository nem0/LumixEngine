#pragma once

#include "allocator.h"
#include "sync.h"

namespace Lumix {

struct LUMIX_ENGINE_API DefaultAllocator final : IAllocator {
	struct Page;

	DefaultAllocator();
	~DefaultAllocator();

	void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;

	u8* m_small_allocations = nullptr;
	Page* m_free_lists[4];
	u32 m_page_count = 0;
	Mutex m_mutex;
};

struct LUMIX_ENGINE_API BaseProxyAllocator final : IAllocator {
	explicit BaseProxyAllocator(IAllocator& source);
	~BaseProxyAllocator();

	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t size) override;
	IAllocator& getSourceAllocator() { return m_source; }

private:
	IAllocator& m_source;
	volatile i32 m_allocation_count;
};

struct LinearAllocator : IAllocator {
	LinearAllocator(u32 reserved);
	~LinearAllocator();

	void reset();
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t size) override;

private:
	u32 m_commited;
	u32 m_reserved;
	u32 m_end;
	u8* m_mem;
};

// only single allocation, can be used by Array<T> to allocate on stack
template <u32 CAPACITY, u32 ALIGN = 8>
struct LUMIX_ENGINE_API StackAllocator final : IAllocator {
	explicit StackAllocator(IAllocator& fallback) : m_fallback(fallback) {}
	~StackAllocator() { ASSERT(!m_allocated); }

	void* allocate_aligned(size_t size, size_t align) override {
		ASSERT(!m_allocated);
		ASSERT(align <= ALIGN);
		m_allocated = true;
		if (size <= CAPACITY) return m_mem;
		return m_fallback.allocate_aligned(size, align);
	}

	void deallocate_aligned(void* ptr) override {
		if (!ptr) return;
		ASSERT(m_allocated);
		m_allocated = false;
		if (ptr == m_mem) return;
		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		m_fallback.deallocate_aligned(ptr);
	}

	void* reallocate_aligned(void* ptr, size_t size, size_t align) override {
		if (!ptr) {
			ASSERT(!m_allocated);
			return allocate_aligned(size, align);
		}
		ASSERT(m_allocated);
		ASSERT(align <= ALIGN);
		if (ptr == m_mem) {
			if (size <= CAPACITY) return m_mem;
			void* n = m_fallback.allocate_aligned(size, align);
			memcpy(n, m_mem, CAPACITY);
			return n;
		}
		
		ASSERT(ptr < m_mem || ptr > m_mem + CAPACITY);
		if (size > CAPACITY) return m_fallback.reallocate_aligned(ptr, size, align);
		memcpy(m_mem, ptr, size);
		m_fallback.deallocate_aligned(ptr);
		return m_mem;
	}

	void* allocate(size_t size) override { ASSERT(false); return nullptr; }
	void deallocate(void* ptr) override { ASSERT(false); }
	void* reallocate(void* ptr, size_t size) override { ASSERT(false); return nullptr; }

private:
	bool m_allocated = false;
	alignas(ALIGN) u8 m_mem[CAPACITY];
	IAllocator& m_fallback;
};

} // namespace Lumix

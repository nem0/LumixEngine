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

}
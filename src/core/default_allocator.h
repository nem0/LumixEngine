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

} // namespace Lumix
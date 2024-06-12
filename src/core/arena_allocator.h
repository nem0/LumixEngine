#pragma once

#include "allocator.h"
#include "atomic.h"
#include "crt.h"
#include "sync.h"

namespace Lumix {


// allocations in a row one after another, deallocate everything at once
// use case: data for one frame
struct LUMIX_CORE_API ArenaAllocator : IAllocator {
	ArenaAllocator(u32 reserved);
	~ArenaAllocator();

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


} // namespace Lumix

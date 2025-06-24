#pragma once

#include "allocator.h"
#include "atomic.h"
#include "sync.h"
#ifdef LUMIX_DEBUG
	#include "debug.h"
	#include "tag_allocator.h"
#endif

namespace Lumix {


// allocations in a row one after another, deallocate everything at once
// use case: data for one frame
struct LUMIX_CORE_API ArenaAllocator : IAllocator {
	ArenaAllocator(u32 reserved, IAllocator& parent, const char* tag);
	~ArenaAllocator();

	void reset();
	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;

private:
	IAllocator& m_parent;
	u32 m_commited_bytes = 0;
	u32 m_reserved;
	AtomicI32 m_end = 0;
	u8* m_mem;
	Mutex m_mutex;
	#ifdef LUMIX_DEBUG
		debug::AllocationInfo m_allocation_info;
		TagAllocator m_tag_allocator;
	#endif
};


} // namespace Lumix

#include "core/allocator.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/log.h"
#include "core/page_allocator.h"
#include "core/os.h"


namespace black
{

PageAllocator::PageAllocator(IAllocator& fallback)
	: free_pages(fallback)
	#ifdef BLACK_DEBUG
		, tag_allocator(fallback, "page allocator")
	#endif
{
	ASSERT(os::getMemPageAlignment() % PAGE_SIZE == 0);
	#ifdef BLACK_DEBUG
		allocation_info.flags = debug::AllocationInfo::IS_PAGED;
		allocation_info.tag = &tag_allocator;
		allocation_info.size = 0;
		debug::registerAlloc(allocation_info);
	#endif
}

PageAllocator::~PageAllocator() {
	ASSERT(allocated_count == 0);
	
	#ifdef BLACK_DEBUG
		debug::unregisterAlloc(allocation_info);
	#endif
	
	void* p;
	while (free_pages.pop(p)) {
		os::memRelease(p, PAGE_SIZE);
	}
}


void* PageAllocator::allocate()
{
	allocated_count.inc();
	
	void* p;
	if (free_pages.pop(p)) return p;
	
	reserved_count.inc();
	#ifdef BLACK_DEBUG
		debug::resizeAlloc(allocation_info, PAGE_SIZE * reserved_count);
	#endif
	void* mem = os::memReserve(PAGE_SIZE);
	ASSERT(uintptr(mem) % PAGE_SIZE == 0);
	os::memCommit(mem, PAGE_SIZE);
	return mem;
}


void PageAllocator::deallocate(void* mem)
{
	allocated_count.dec();
	free_pages.push(mem);
}


} // namespace black
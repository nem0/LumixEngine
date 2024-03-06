#include "core/allocator.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/log.h"
#include "core/page_allocator.h"
#include "core/os.h"


namespace Lumix
{

PageAllocator::PageAllocator(IAllocator& fallback)
	: free_pages(fallback)
{
	ASSERT(os::getMemPageAlignment() % PAGE_SIZE == 0);
}

PageAllocator::~PageAllocator()
{
	ASSERT(allocated_count == 0);
	
	void* p;
	while (free_pages.pop(p)) {
		os::memRelease(p, PAGE_SIZE);
	}
	while (free_pages.popSecondary(p)) {
		os::memRelease(p, PAGE_SIZE);
	}
}


void PageAllocator::lock()
{
	mutex.enter();
}


void PageAllocator::unlock()
{
	mutex.exit();
}


void* PageAllocator::allocate(bool lock)
{
	allocated_count.inc();

	void* p;
	if (free_pages.pop(p)) return p;
	
	if (lock) mutex.enter();
	if (free_pages.popSecondary(p)) {
		if (lock) mutex.exit();
		return p;
	}

	++reserved_count;
	if (lock) mutex.exit();
	void* mem = os::memReserve(PAGE_SIZE);
	ASSERT(uintptr(mem) % PAGE_SIZE == 0);
	os::memCommit(mem, PAGE_SIZE);
	return mem;
}


void PageAllocator::deallocate(void* mem, bool lock)
{
	allocated_count.dec();
	free_pages.push(mem, lock ? &mutex : nullptr);
}


} // namespace Lumix
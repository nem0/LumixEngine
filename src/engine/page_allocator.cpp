#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#include "engine/page_allocator.h"
#include "engine/os.h"


namespace Lumix
{


PageAllocator::~PageAllocator()
{
	ASSERT(allocated_count == 0);
	void* p = free_pages;
	while (p) {
		void* tmp = p;
		memcpy(&p, p, sizeof(p)); //-V579
		os::memRelease(tmp, PAGE_SIZE);
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
	if (lock) mutex.enter();
	++allocated_count;
	if (free_pages) {
		void* tmp = free_pages;
		memcpy(&free_pages, free_pages, sizeof(free_pages)); //-V579
		if (lock) mutex.exit();
		return tmp;
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
	if (lock) mutex.enter();
	--allocated_count;
	memcpy(mem, &free_pages, sizeof(free_pages));
	free_pages = mem;
	if (lock) mutex.exit();
}


} // namespace Lumix
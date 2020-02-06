#include "engine/crt.h"
#include "engine/mt/atomic.h"
#include "engine/page_allocator.h"
#include "engine/win/simple_win.h"


namespace Lumix
{


PageAllocator::~PageAllocator()
{
	ASSERT(allocated_count == 0);
	void* p = free_pages;
	while (p) {
		void* tmp = p;
		memcpy(&p, p, sizeof(p));
		VirtualFree(tmp, 0, MEM_RELEASE);
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
		memcpy(&free_pages, free_pages, sizeof(free_pages));
		if (lock) mutex.exit();
		return tmp;
	}
	++reserved_count;
	if (lock) mutex.exit();
	return VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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
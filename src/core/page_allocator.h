#pragma once


#include "allocator.h"
#include "atomic.h"
#include "debug.h"
#include "ring_buffer.h"
#include "sync.h"
#include "tag_allocator.h"


namespace Lumix
{


struct LUMIX_CORE_API PageAllocator final {
	enum { PAGE_SIZE = 4096 };

	PageAllocator(IAllocator& fallback);
	~PageAllocator();
		
	void* allocate();
	void deallocate(void* mem);

private:
	AtomicI32 allocated_count = 0;
	AtomicI32 reserved_count = 0;
	RingBuffer<void*, 512> free_pages;
	debug::AllocationInfo allocation_info;
	#ifdef LUMIX_DEBUG
		TagAllocator tag_allocator;
	#endif
};


template <typename T>
struct PagedListIterator
{
	PagedListIterator(T* value) : value(value) {}

	PagedListIterator(const PagedListIterator& rhs) = delete;
	void operator=(const PagedListIterator& rhs) = delete;

	T* next() {
		for (;;) {
			volatile T* tmp = value;
			if(!tmp) return nullptr;
			if (compareExchangePtr((void*volatile*)&value, (void*)tmp->header.next, (void*)tmp)) return (T*)tmp;
		}
	}

private:
	volatile T* value;
};


template <typename T>
struct PagedList
{
	PagedList(PageAllocator& allocator)
		: allocator(allocator)
	{}


	~PagedList()
	{
		T* i = begin;
		while(i) {
			T* tmp = i;
			i = i->header.next;
			allocator.deallocate(tmp);
		}
	}


	PagedList(const PagedList& rhs) = delete;
	void operator=(const PagedList& rhs) = delete;


	T* detach()
	{
		T* tmp = begin;
		begin = end = nullptr;
		return tmp;
	}


	T* push()
	{
		MutexGuard guard(mutex);
		void* mem = allocator.allocate();
		T* page = new (NewPlaceholder(), mem) T;
		if(!begin) {
			begin = end = page;
		}
		else {
			end->header.next = page;
			page->header.next = nullptr;
			end = page;
		}
		return page;
	}


	T* begin = nullptr;
	T* end = nullptr;
	PageAllocator& allocator;
	Mutex mutex;
};


} // namespace Lumix
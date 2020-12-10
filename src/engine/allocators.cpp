#include "engine/allocators.h"
#include "engine/atomic.h"
#include "engine/crt.h"
#include "engine/math.h"
#include "engine/os.h"
#ifndef _WIN32
	#include <string.h>
	#include <malloc.h>
#else
	#include <intrin.h>
#endif


namespace Lumix
{
	static constexpr u32 PAGE_SIZE = 4096;
	static constexpr size_t MAX_PAGE_COUNT = 16384;
	static constexpr u32 SMALL_ALLOC_MAX_SIZE = 64;

	struct DefaultAllocator::Page {
		struct Header {
			Page* prev;
			Page* next;
			u32 first_free;
			u32 item_size;
		};
		u8 data[PAGE_SIZE - sizeof(Header)];
		Header header;
	};

	static_assert(sizeof(DefaultAllocator::Page) == PAGE_SIZE);

	static u32 sizeToBin(size_t n) {
		ASSERT(n > 0);
		ASSERT(n <= SMALL_ALLOC_MAX_SIZE);
		#ifdef _WIN32
			unsigned long res;
			return _BitScanReverse(&res, ((unsigned long)n - 1) >> 2) ? res : 0;
		#else
			return 31 - __builtin_clz((n - 1) >> 2);
		#endif
	}

	void initPage(u32 item_size, DefaultAllocator::Page* page) {
		os::memCommit(page, PAGE_SIZE);
		page = new (NewPlaceholder(), page) DefaultAllocator::Page;
		page->header.first_free = 0;
		page->header.prev = nullptr;
		page->header.next = nullptr;
		page->header.item_size = item_size;

		for (u32 i = 0; i < sizeof(page->data) / item_size; ++i) {
			*(u32*)&page->data[i * item_size] = u32(i * item_size + item_size);
		}
	}

	static DefaultAllocator::Page* getPage(void* ptr) {
		return (DefaultAllocator::Page*)((uintptr)ptr & ~u64(PAGE_SIZE - 1));
	}

	static void freeSmall(DefaultAllocator& allocator, void* mem) {
		u8* ptr = (u8*)mem;
		DefaultAllocator::Page* page = getPage(ptr);
		
		MutexGuard guard(allocator.m_mutex);
		if (page->header.first_free + page->header.item_size > sizeof(page->data)) {
			ASSERT(!page->header.next);
			ASSERT(!page->header.prev);
			const u32 bin = sizeToBin(page->header.item_size);
			page->header.next = allocator.m_free_lists[bin];
			allocator.m_free_lists[bin] = page;
		}

		*(u32*)ptr = page->header.first_free;
		page->header.first_free = u32(ptr - page->data);
	}

	static void* reallocSmall(DefaultAllocator& allocator, void* mem, size_t n) {
		DefaultAllocator::Page* p = getPage(mem);
		if (n <= SMALL_ALLOC_MAX_SIZE) {
			const u32 bin = sizeToBin(n);
			if (sizeToBin(p->header.item_size) == bin) return mem;
		}
		
		void* new_mem = allocator.allocate(n);
		memcpy(new_mem, mem, minimum((size_t)p->header.item_size, n));
		allocator.deallocate(mem);
		return new_mem;
	}
	
	static void* reallocSmallAligned(DefaultAllocator& allocator, void* mem, size_t n, size_t align) {
		DefaultAllocator::Page* p = getPage(mem);
		if (n <= SMALL_ALLOC_MAX_SIZE) {
			const u32 bin = sizeToBin(n);
			if (sizeToBin(p->header.item_size) == bin) return mem;
		}
		
		void* new_mem = allocator.allocate_aligned(n, align);
		memcpy(new_mem, mem, minimum((size_t)p->header.item_size, n));
		allocator.deallocate_aligned(mem);
		return new_mem;
	}

	static void* allocSmall(DefaultAllocator& allocator, size_t n) {
		if (!allocator.m_small_allocations) {
			allocator.m_small_allocations = (u8*)os::memReserve(PAGE_SIZE * MAX_PAGE_COUNT);
		}

		const u32 bin = sizeToBin(n);

		MutexGuard guard(allocator.m_mutex);
		DefaultAllocator::Page* p = allocator.m_free_lists[bin];
		if (!p) {
			if (allocator.m_page_count == MAX_PAGE_COUNT) return nullptr;

			p = (DefaultAllocator::Page*)(allocator.m_small_allocations + PAGE_SIZE * allocator.m_page_count);
			initPage(8 << bin, p);
			allocator.m_free_lists[bin] = p;
			++allocator.m_page_count;
		}

		ASSERT(p->header.item_size > 0);
		ASSERT(p->header.first_free + n < sizeof(p->data));
		void* res = &p->data[p->header.first_free];
		p->header.first_free = *(u32*)res;

		const bool is_page_full = p->header.first_free + p->header.item_size > sizeof(p->data);
		if (is_page_full) {
			if (allocator.m_free_lists[bin] == p) {
				allocator.m_free_lists[bin] = p->header.next;
			}
			if (p->header.next) {
				p->header.next->header.prev = p->header.prev;
			}
			if (p->header.prev) {
				p->header.prev->header.next = p->header.next;
			}
			p->header.next = p->header.prev = nullptr;
		}

		return res;
	}

	static bool isSmallAlloc(DefaultAllocator& allocator, void* p) {
		return allocator.m_small_allocations && p >= allocator.m_small_allocations && p < allocator.m_small_allocations + (PAGE_SIZE * MAX_PAGE_COUNT);
	}

	DefaultAllocator::DefaultAllocator() {
		m_page_count = 0;
		memset(m_free_lists, 0, sizeof(m_free_lists));
	}

	DefaultAllocator::~DefaultAllocator() {
		os::memRelease(m_small_allocations);
	}

	void* DefaultAllocator::allocate(size_t n)
	{
		if (n <= SMALL_ALLOC_MAX_SIZE) {
			return allocSmall(*this, n);
		}
		return malloc(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		if (isSmallAlloc(*this, p)) {
			freeSmall(*this, p);
			return;
		}
		free(p);
	}


	void* DefaultAllocator::reallocate(void* ptr, size_t size)
	{
		if (isSmallAlloc(*this, ptr)) {
			return reallocSmall(*this, ptr, size);
		}
		return realloc(ptr, size);
	}

#ifdef _WIN32
	void* DefaultAllocator::allocate_aligned(size_t size, size_t align)
	{
		if (size <= SMALL_ALLOC_MAX_SIZE && align <= size) {
			return allocSmall(*this, size);
		}
		return _aligned_malloc(size, align);
	}


	void DefaultAllocator::deallocate_aligned(void* ptr)
	{
		if (isSmallAlloc(*this, ptr)) {
			freeSmall(*this, ptr);
			return;
		}
		_aligned_free(ptr);
	}


	void* DefaultAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
	{
		if (isSmallAlloc(*this, ptr)) {
			return reallocSmallAligned(*this, ptr, size, align);
		}
		return _aligned_realloc(ptr, size, align);
	}
#else
	void* DefaultAllocator::allocate_aligned(size_t size, size_t align)
	{
		return aligned_alloc(align, size);
	}


	void DefaultAllocator::deallocate_aligned(void* ptr)
	{
		free(ptr);
	}


	void* DefaultAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
	{
		// POSIX and glibc do not provide a way to realloc with alignment preservation
		if (size == 0) {
			free(ptr);
			return nullptr;
		}
		void* newptr = aligned_alloc(align, size);
		if (newptr == nullptr) {
			return nullptr;
		}
		memcpy(newptr, ptr, malloc_usable_size(ptr));
		free(ptr);
		return newptr;
	}
#endif

	
BaseProxyAllocator::BaseProxyAllocator(IAllocator& source)
	: m_source(source)
{
	m_allocation_count = 0;
}

BaseProxyAllocator::~BaseProxyAllocator() { ASSERT(m_allocation_count == 0); }


void* BaseProxyAllocator::allocate_aligned(size_t size, size_t align)
{
	atomicIncrement(&m_allocation_count);
	return m_source.allocate_aligned(size, align);
}


void BaseProxyAllocator::deallocate_aligned(void* ptr)
{
	if(ptr)
	{
		atomicDecrement(&m_allocation_count);
		m_source.deallocate_aligned(ptr);
	}
}


void* BaseProxyAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
{
	if (!ptr) atomicIncrement(&m_allocation_count);
	if (size == 0) atomicDecrement(&m_allocation_count);
	return m_source.reallocate_aligned(ptr, size, align);
}


void* BaseProxyAllocator::allocate(size_t size)
{
	atomicIncrement(&m_allocation_count);
	return m_source.allocate(size);
}

void BaseProxyAllocator::deallocate(void* ptr)
{
	if (ptr)
	{
		atomicDecrement(&m_allocation_count);
		m_source.deallocate(ptr);
	}
}

void* BaseProxyAllocator::reallocate(void* ptr, size_t size)
{
	if (!ptr) atomicIncrement(&m_allocation_count);
	if (size == 0) atomicDecrement(&m_allocation_count);
	return m_source.reallocate(ptr, size);
}


} // namespace Lumix

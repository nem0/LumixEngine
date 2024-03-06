#include "core/allocators.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/math.h"
#include "core/os.h"
#if !defined __linux__ && defined __clang__
	#include <intrin.h>
#endif
#if !defined _WIN32 || defined __clang__
	#include <malloc.h>
	#include <string.h>
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
			size_t tmp = (n - 1) >> 2;
			auto res = tmp == 0 ? 0 : 31 - __builtin_clz(tmp);
			ASSERT(res <= lengthOf(((DefaultAllocator*)nullptr)->m_free_lists));
			return res;
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
	
	static void* reallocSmallAligned(DefaultAllocator& allocator, void* mem, size_t n, size_t align) {
		DefaultAllocator::Page* p = getPage(mem);
		if (n <= SMALL_ALLOC_MAX_SIZE) {
			const u32 bin = sizeToBin(n);
			if (sizeToBin(p->header.item_size) == bin) return mem;
		}
		
		void* new_mem = allocator.allocate(n, align);
		memcpy(new_mem, mem, minimum((size_t)p->header.item_size, n));
		allocator.deallocate(mem);
		return new_mem;
	}

	static void* allocSmall(DefaultAllocator& allocator, size_t n) {
		const u32 bin = sizeToBin(n);

		MutexGuard guard(allocator.m_mutex);

		if (!allocator.m_small_allocations) {
			allocator.m_small_allocations = (u8*)os::memReserve(PAGE_SIZE * MAX_PAGE_COUNT);
		}
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
		os::memRelease(m_small_allocations, PAGE_SIZE * MAX_PAGE_COUNT);
	}

#ifdef _WIN32
	void* DefaultAllocator::allocate(size_t size, size_t align)
	{
		if (size <= SMALL_ALLOC_MAX_SIZE && align <= size) {
			return allocSmall(*this, size);
		}
		return _aligned_malloc(size, align);
	}


	void DefaultAllocator::deallocate(void* ptr)
	{
		if (isSmallAlloc(*this, ptr)) {
			freeSmall(*this, ptr);
			return;
		}
		_aligned_free(ptr);
	}


	void* DefaultAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align)
	{
		if (isSmallAlloc(*this, ptr)) {
			return reallocSmallAligned(*this, ptr, new_size, align);
		}
		return _aligned_realloc(ptr, new_size, align);
	}
#else
	void* DefaultAllocator::allocate(size_t size, size_t align)
	{
		if (size <= SMALL_ALLOC_MAX_SIZE && align <= size) {
			return allocSmall(*this, size);
		}
		return aligned_alloc(align, size);
	}


	void DefaultAllocator::deallocate(void* ptr)
	{
		if (isSmallAlloc(*this, ptr)) {
			freeSmall(*this, ptr);
			return;
		}
		free(ptr);
	}


	void* DefaultAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align)
	{
		if (isSmallAlloc(*this, ptr)) {
			return reallocSmallAligned(*this, ptr, new_size, align);
		}
		// POSIX and glibc do not provide a way to realloc with alignment preservation
		if (new_size == 0) {
			free(ptr);
			return nullptr;
		}
		void* newptr = aligned_alloc(align, new_size);
		if (newptr == nullptr) {
			return nullptr;
		}
		if (ptr) memcpy(newptr, ptr, minimum(new_size, old_size));
		free(ptr);
		return newptr;
	}
#endif

	
BaseProxyAllocator::BaseProxyAllocator(IAllocator& source)
	: m_source(source)
	, m_allocation_count(0)
{
}

BaseProxyAllocator::~BaseProxyAllocator() { ASSERT(m_allocation_count == 0); }


void* BaseProxyAllocator::allocate(size_t size, size_t align)
{
	m_allocation_count.inc();
	return m_source.allocate(size, align);
}


void BaseProxyAllocator::deallocate(void* ptr)
{
	if(ptr)
	{
		m_allocation_count.dec();
		m_source.deallocate(ptr);
	}
}


void* BaseProxyAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align)
{
	if (!ptr) m_allocation_count.inc();
	if (new_size == 0) m_allocation_count.dec();
	return m_source.reallocate(ptr, new_size, old_size, align);
}

LinearAllocator::LinearAllocator(u32 reserved)
{
	m_reserved = reserved;
	m_mem = (u8*)os::memReserve(reserved);
}

LinearAllocator::~LinearAllocator() {
	ASSERT(m_end == 0);
	os::memRelease(m_mem, m_reserved);
	g_total_commited_bytes.subtract(m_commited_bytes);
}

void LinearAllocator::reset() {
	m_end = 0;
}

static u32 roundUp(u32 val, u32 align) {
	ASSERT(isPowOfTwo(align));
	return (val + align - 1) & ~(align - 1);
}

void* LinearAllocator::allocate(size_t size, size_t align) {
	ASSERT(size < 0xffFFffFF);
	u32 start;
	for (;;) {
		const u32 end = m_end;
		start = roundUp(end, (u32)align);
		if (m_end.compareExchange(u32(start + size), end)) break;
	}

	if (start + size <= m_commited_bytes) return m_mem + start;

	MutexGuard guard(m_mutex);
	if (start + size <= m_commited_bytes) return m_mem + start;

	const u32 commited = roundUp(start + (u32)size, 4096);
	ASSERT(commited < m_reserved);
	os::memCommit(m_mem + m_commited_bytes, commited - m_commited_bytes);
	g_total_commited_bytes.add(commited - m_commited_bytes);
	m_commited_bytes = commited;

	return m_mem + start;
}

void LinearAllocator::deallocate(void* ptr) { /*everything should be "deallocated" with reset()*/ }
void* LinearAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) { 
	if (!ptr) return allocate(new_size, align);
	// realloc not supported
	ASSERT(false); 
	return nullptr;
}

TagAllocator::TagAllocator(IAllocator& allocator, const char* tag_name)
	: m_tag(tag_name)
{
	m_effective_allocator = m_direct_parent = &allocator;
	while (m_effective_allocator->getParent() && m_effective_allocator->isTagAllocator()) {
		m_effective_allocator = m_effective_allocator->getParent();
	}
}

thread_local TagAllocator* active_allocator = nullptr;

void* TagAllocator::allocate(size_t size, size_t align) {
	active_allocator = this;
	return m_effective_allocator->allocate(size, align);
}

void TagAllocator::deallocate(void* ptr) {
	m_effective_allocator->deallocate(ptr);
}

void* TagAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) {
	active_allocator = this;
	return m_effective_allocator->reallocate(ptr, new_size, old_size, align);
}

TagAllocator* TagAllocator::getActiveAllocator() {
	return active_allocator;
}

IAllocator& getGlobalAllocator() {
	static DefaultAllocator alloc;
	return alloc;
}

} // namespace Lumix

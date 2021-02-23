#pragma once

#include "allocator.h"
#include "os.h"
#include "sync.h"

namespace Lumix {

struct LUMIX_ENGINE_API DefaultAllocator final : IAllocator {
	struct Page;

	DefaultAllocator();
	~DefaultAllocator();

	void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;

	u8* m_small_allocations = nullptr;
	Page* m_free_lists[4];
	u32 m_page_count = 0;
	Mutex m_mutex;
};


struct LUMIX_ENGINE_API BaseProxyAllocator final : IAllocator {
	explicit BaseProxyAllocator(IAllocator& source);
	~BaseProxyAllocator();

	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t size) override;
	IAllocator& getSourceAllocator() { return m_source; }

private:
	IAllocator& m_source;
	volatile i32 m_allocation_count;
};

template <typename T>
struct VirtualAllocator {
	VirtualAllocator(u32 count) {
		max_count = count;
		mem = (u8*)os::memReserve(count * sizeof(T));
		commited = 0;
		page_size = os::getMemPageSize();
		ASSERT(page_size % sizeof(T) == 0);
		ASSERT(sizeof(T) >= sizeof(u32));
		first_free = -1;
	}

	void initPage(u8* page, u32 page_idx) {
		const u32 per_page_count = page_size / sizeof(T);
		const i32 offset = page_idx * per_page_count;
		for (i32 i = 0; i < (i32)per_page_count - 1; ++i) {
			*(i32*)(page + sizeof(T) * i) = i + 1 + offset;
		}
		*(i32*)(page + sizeof(T) * (per_page_count - 1)) = first_free;
		first_free = offset;
	}

	~VirtualAllocator() {
		os::memRelease(mem);
	}

	T* alloc() {
		if (first_free == -1) {
			os::memCommit(mem + page_size * commited, page_size);
			initPage(mem + page_size * commited, commited);
			++commited;
		}

		const i32 next_free = *(i32*)(mem + sizeof(T) * first_free);

		T* res = new (NewPlaceholder(), (T*)mem + first_free) T;
		first_free = next_free;
		return res;
	}

	u32 getID(const T* ptr) const { return u32(ptr - (T*)mem); }
	T& getObject(u32 id) { return ((T*)mem)[id]; }
	const T& getObject(u32 id) const { return ((T*)mem)[id]; }

	void dealloc(T* ptr) {
		ptr->~T();
		i32 idx = i32(ptr - (T*)mem);
		i32* n = new (NewPlaceholder(), ptr);
		*n = first_free;
		first_free = idx;
	}

	u8* mem;
	u32 commited;
	u32 page_size;
	i32 first_free;
	u32 max_count;
};

} // namespace Lumix
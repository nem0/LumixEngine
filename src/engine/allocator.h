#pragma once 


#ifndef _WIN32
	#include <new>
#endif
#include "engine/lumix.h"

#define LUMIX_NEW(allocator, ...) new (Lumix::NewPlaceholder(), (allocator).allocate_aligned(sizeof(__VA_ARGS__), alignof(__VA_ARGS__))) __VA_ARGS__
#define LUMIX_DELETE(allocator, var) (allocator).deleteObject(var);

namespace Lumix { struct NewPlaceholder {}; }
inline void* operator new(size_t, Lumix::NewPlaceholder, void* where) { return where; }
inline void operator delete(void*, Lumix::NewPlaceholder,  void*) { } 

namespace Lumix {

struct LUMIX_ENGINE_API IAllocator {
	virtual ~IAllocator() {}
	virtual bool isDebug() const { return false; }

	virtual void* allocate(size_t size) = 0;
	virtual void deallocate(void* ptr) = 0;
	virtual void* reallocate(void* ptr, size_t size) = 0;

	virtual void* allocate_aligned(size_t size, size_t align) = 0;
	virtual void deallocate_aligned(void* ptr) = 0;
	virtual void* reallocate_aligned(void* ptr, size_t size, size_t align) = 0;

	template <typename T> void deleteObject(T* ptr) {
		if (ptr)
		{
			ptr->~T();
			deallocate_aligned(ptr);
		}
	}
};


struct LUMIX_ENGINE_API DefaultAllocator final : IAllocator {
	void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
};


struct LUMIX_ENGINE_API BaseProxyAllocator final : IAllocator
{
public:
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

} // namespace Lumix
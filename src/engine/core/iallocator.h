#pragma once 


#include "lumix.h"


namespace Lumix
{
	struct NewPlaceholder {};
}


inline void* operator new(size_t, Lumix::NewPlaceholder, void* where)
{
	return where;
}


inline void operator delete(void*, Lumix::NewPlaceholder,  void*)
{
}


namespace Lumix
{

#define ALIGN_OF(T) __alignof(T)
#define LUMIX_NEW(allocator, type) new (Lumix::NewPlaceholder(), (allocator).allocate(sizeof(type))) type
#define LUMIX_DELETE(allocator, var) (allocator).deleteObject(var);


class LUMIX_ENGINE_API IAllocator
{
public:
	virtual ~IAllocator() {}

	virtual void* allocate(size_t size) = 0;
	virtual void deallocate(void* ptr) = 0;
	virtual void* reallocate(void* ptr, size_t size) = 0;

	virtual void* allocate_aligned(size_t size, size_t align) = 0;
	virtual void deallocate_aligned(void* ptr) = 0;
	virtual void* reallocate_aligned(void* ptr, size_t size, size_t align) = 0;

	template <class T> void deleteObject(T* ptr)
	{
		if (ptr)
		{
			ptr->~T();
			deallocate(ptr);
		}
	}
};


} // namespace Lumix
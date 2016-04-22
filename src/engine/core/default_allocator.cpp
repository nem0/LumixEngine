#include "engine/core/default_allocator.h"
#include <cstdlib>


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return malloc(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		free(p);
	}


	void* DefaultAllocator::reallocate(void* ptr, size_t size)
	{
		return realloc(ptr, size);
	}

#ifdef _WIN32
	void* DefaultAllocator::allocate_aligned(size_t size, size_t align)
	{
		return _aligned_malloc(size, align);
	}


	void DefaultAllocator::deallocate_aligned(void* ptr)
	{
		_aligned_free(ptr);
	}


	void* DefaultAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
	{
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
		memcpy(newptr, ptr, size);
		free(ptr);
		return newptr;
	}
#endif


} // ~namespace Lumix

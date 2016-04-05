#include "core/default_allocator.h"
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

} // ~namespace Lumix

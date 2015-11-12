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


} // ~namespace Lumix

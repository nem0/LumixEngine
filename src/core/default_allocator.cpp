#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return (uint8_t*)malloc(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		free(p);
	}


} // ~namespace Lumix

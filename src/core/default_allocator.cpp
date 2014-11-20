#include "core/default_allocator.h"


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


} // ~namespace Lumix

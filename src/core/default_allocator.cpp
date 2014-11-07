#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return LUMIX_MALLOC(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		LUMIX_FREE(p);
	}


} // ~namespace Lumix

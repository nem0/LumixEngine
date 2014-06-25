#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return LUMIX_MALLOC(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		LUMIX_FREE((uint8_t*)p);
	}


	void* DefaultAllocator::reallocate(void* p, size_t n)
	{
		return LUMIX_REALLOC(p, n);
	}

} // ~namespace Lumix

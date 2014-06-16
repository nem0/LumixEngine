#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return LUX_MALLOC(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		LUX_FREE((uint8_t*)p);
	}


	void* DefaultAllocator::reallocate(void* p, size_t n)
	{
		return LUX_REALLOC(p, n);
	}

} // ~namespace Lumix

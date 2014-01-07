#include "core/default_allocator.h"


namespace Lux
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return LUX_NEW_ARRAY(uint8_t, n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		LUX_DELETE_ARRAY((uint8_t*)p);
	}


	void* DefaultAllocator::reallocate(void* p, size_t n)
	{
		return LUX_REALLOC(p, n);
	}

} // ~namespace Lux
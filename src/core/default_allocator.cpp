#include "core/default_allocator.h"


namespace Lux
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return LUX_NEW_ARRAY(uint8_t, n);
	}


	void DefaultAllocator::deallocate(void* p, size_t n)
	{
		LUX_DELETE_ARRAY((uint8_t*)p);
	}


} // ~namespace Lux
#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		uint8_t* p = (uint8_t*)malloc(n);
		return p;
	}


	void DefaultAllocator::deallocate(void* p)
	{
		if (p)
		{
			free(p);
		}
	}


} // ~namespace Lumix

#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		uint8_t* p = (uint8_t*)LUMIX_MALLOC(n + sizeof(void*));
		*(void**)p = this;
		return p + sizeof(void*);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		if (p)
		{
			void* actual_mem = (void*)((uint8_t*)p - sizeof(void*));
			ASSERT(*(void**)actual_mem == this);
			LUMIX_FREE(actual_mem);
		}
	}


} // ~namespace Lumix

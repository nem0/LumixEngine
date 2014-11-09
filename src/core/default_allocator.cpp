#include "core/default_allocator.h"


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		uint8_t* p = (uint8_t*)LUMIX_MALLOC(n + sizeof(size_t) + sizeof(void*));
		*(void**)p = this;
		*(size_t*)(p + sizeof(void*)) = n;
		MT::atomicAdd(&m_total_size, (int32_t)n);
		return p + 2*sizeof(void*);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		if (p)
		{
			void* actual_mem = (void*)((uint8_t*)p - 2*sizeof(void*));
			ASSERT(*(void**)actual_mem == this);
			size_t n = *(size_t*)((uint8_t*)actual_mem + sizeof(void*));
			MT::atomicSubtract(&m_total_size, (int32_t)n);
			LUMIX_FREE(actual_mem);
		}
	}


} // ~namespace Lumix

#include "core/default_allocator.h"


namespace Lux
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return new uint8_t[n];
	}


	void DefaultAllocator::deallocate(void* p, size_t n)
	{
		delete[] (uint8_t*)p;
	}


} // ~namespace Lux
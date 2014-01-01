#pragma once


#include "core/lux.h"


namespace Lux
{

	class LUX_CORE_API DefaultAllocator
	{
		public:
			void* allocate(size_t n);
			void deallocate(void* p, size_t n);
	};


} // ~namespace Lux
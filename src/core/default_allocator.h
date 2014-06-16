#pragma once


#include "core/lumix.h"


namespace Lumix
{

	class LUX_CORE_API DefaultAllocator
	{
		public:
			void* allocate(size_t n);
			void deallocate(void* p);
			void* reallocate(void* p, size_t n);
	};


} // ~namespace Lumix

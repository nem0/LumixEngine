#pragma once


#include "core/lumix.h"
#include "core/iallocator.h"


namespace Lumix
{

	class LUMIX_CORE_API DefaultAllocator : public IAllocator
	{
		public:
			DefaultAllocator()
				: m_total_size(0)
			{ }

			virtual void* allocate(size_t n) override;
			virtual void deallocate(void* p) override;

		private:
			volatile int32_t m_total_size;
	};


} // ~namespace Lumix

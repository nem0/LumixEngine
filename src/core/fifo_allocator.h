#pragma once


#include "core/lumix.h"
#include "core/mt/spin_mutex.h"


namespace Lumix
{


	/// FIFOAllocator uses fixed ring buffer to allocate memory in FIFO order.
	class LUMIX_CORE_API FIFOAllocator
	{
		public:
			FIFOAllocator(size_t buffer_size);
			~FIFOAllocator();

			void* allocate(size_t n);
			void deallocate(void* p);
			void* reallocate(void* p, size_t n);
		
		private:
			size_t m_buffer_size;
			uint8_t* m_buffer;
			int32_t m_start;
			int32_t m_end;
			MT::SpinMutex m_mutex;
	};


} // ~namespace Lumix

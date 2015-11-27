#pragma once


#include "lumix.h"
#include "core/iallocator.h"


namespace Lumix
{

	template <size_t SIZE>
	class StackAllocator : public IAllocator
	{
		public:
			StackAllocator()
			{
				m_end = 0;
			}

			void* allocate(size_t n) override
			{
				ASSERT(n + m_end <= SIZE);
				size_t end = m_end;
				m_end += n;
				return m_data + end;
			}

			void deallocate(void* p) override
			{
				if(p != nullptr)
				{
					ASSERT(p >= m_data && p < m_data + SIZE);
					m_end = ((uint8*)p) - m_data;
				}
			}

			void* reallocate(void*, size_t) override
			{
				ASSERT(false);
				return nullptr;
			}


			void* allocate_aligned(size_t size, size_t align) override { ASSERT(false); return nullptr; };
			void deallocate_aligned(void* ptr) override { ASSERT(false); }
			void* reallocate_aligned(void* ptr, size_t size, size_t align) override { ASSERT(false); return nullptr; }


		private:
			size_t m_end;
			uint8 m_data[SIZE];
	};


} // ~namespace Lumix

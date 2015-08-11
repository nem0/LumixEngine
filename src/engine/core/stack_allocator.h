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

			virtual void* allocate(size_t n) override
			{
				ASSERT(n + m_end <= SIZE);
				size_t end = m_end;
				m_end += n;
				return m_data + end;
			}

			virtual void deallocate(void* p) override
			{
				if(p != nullptr)
				{
					ASSERT(p >= m_data && p < m_data + SIZE);
					m_end = ((uint8_t*)p) - m_data;
				}
			}

		private:
			size_t m_end;
			uint8_t m_data[SIZE];
	};


} // ~namespace Lumix

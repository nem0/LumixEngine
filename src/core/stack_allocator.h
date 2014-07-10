#pragma once


#include "core/lumix.h"


namespace Lumix
{

	template <size_t SIZE>
	class StackAllocator
	{
		public:
			StackAllocator();

			void* allocate(size_t n);
			void deallocate(void* p);
			void* reallocate(void* p, size_t n);

		private:
			size_t m_end;
			uint8_t m_data[SIZE];
	};


	template <size_t SIZE>
	StackAllocator<SIZE>::StackAllocator()
	{
		m_end = 0;
	}


	template <size_t SIZE>
	void* StackAllocator<SIZE>::allocate(size_t n)
	{
		ASSERT(n + m_end <= SIZE);
		size_t end = m_end;
		m_end += n;
		return m_data + end;
	}


	template <size_t SIZE>
	void StackAllocator<SIZE>::deallocate(void* p)
	{
		if(p != NULL)
		{
			ASSERT(p >= m_data && p < m_data + SIZE);
			m_end = ((uint8_t*)p) - m_data;
		}
	}


	template <size_t SIZE>
	void* StackAllocator<SIZE>::reallocate(void* p, size_t n)
	{
		ASSERT(p >= m_data && p < m_data + SIZE);
		m_end = ((uint8_t*)p) - m_data + n;
		return p;
	}



} // ~namespace Lumix

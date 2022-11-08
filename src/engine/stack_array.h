#pragma once

#include "allocators.h"
#include "array.h"

namespace Lumix {

// Array with built-in storage
template <typename T, u32 N>
struct StackArray : Array<T> {
	StackArray(IAllocator& fallback)
		: Array(m_stack_allocator)
		, m_stack_allocator(fallback)
	{
		reserve(N);
	}
	
	~StackArray() {
		clear();
		if (m_data) m_allocator.deallocate_aligned(m_data);
		m_data = nullptr;
		m_capacity = 0;
	}

private:
	StackAllocator<sizeof(T) * N, alignof(T)> m_stack_allocator;
};


} // namespace Lumix

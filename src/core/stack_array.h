#pragma once

#include "array.h"
#include "stack_allocator.h"

namespace Lumix {

// Array with built-in storage
template <typename T, u32 N>
struct StackArray : Array<T> {
	StackArray(IAllocator& fallback)
		: Array<T>(m_stack_allocator)
		, m_stack_allocator(fallback)
	{
		Array<T>::reserve(N);
	}
	
	~StackArray() {
		this->clear();
		if (this->m_data) this->m_allocator.deallocate(this->m_data);
		this->m_data = nullptr;
		this->m_capacity = 0;
	}

private:
	StackAllocator<sizeof(T) * N, alignof(T)> m_stack_allocator;
};


} // namespace Lumix

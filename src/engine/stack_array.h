#pragma once

#include "allocators.h"
#include "array.h"

namespace Lumix {

template <typename T, u32 N>
struct StackArray {
	StackArray(IAllocator& fallback)
		: allocator(fallback)
		, array(allocator)
	{
		array.reserve(N);
	}

	bool empty() const { return array.empty(); }
	u32 size() const { return array.size(); }
	T& emplace() { return array.emplace(); }
	T* begin() { return array.begin(); }
	T* end() { return array.end(); }
	const T* begin() const { return array.begin(); }
	const T* end() const { return array.end(); }
	void push(T&& val) { array.push(static_cast<T&&>(val)); }
	void push(const T& val) { array.push(val); }
	void resize(u32 size) { array.resize(size); }
	T& operator[](u32 idx) { return array[idx]; }
	const T& last() const { return array.last(); }
	void pop() { array.pop(); }

private:
	StackAllocator<sizeof(T) * N, alignof(T)> allocator;
	Array<T> array;
};


} // namespace Lumix

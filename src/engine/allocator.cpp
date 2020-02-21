#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#ifndef _WIN32
	#include <string.h>
	#include <malloc.h>
#endif


namespace Lumix
{


	void* DefaultAllocator::allocate(size_t n)
	{
		return malloc(n);
	}


	void DefaultAllocator::deallocate(void* p)
	{
		free(p);
	}


	void* DefaultAllocator::reallocate(void* ptr, size_t size)
	{
		return realloc(ptr, size);
	}

#ifdef _WIN32
	void* DefaultAllocator::allocate_aligned(size_t size, size_t align)
	{
		return _aligned_malloc(size, align);
	}


	void DefaultAllocator::deallocate_aligned(void* ptr)
	{
		_aligned_free(ptr);
	}


	void* DefaultAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
	{
		return _aligned_realloc(ptr, size, align);
	}
#else
	void* DefaultAllocator::allocate_aligned(size_t size, size_t align)
	{
		return aligned_alloc(align, size);
	}


	void DefaultAllocator::deallocate_aligned(void* ptr)
	{
		free(ptr);
	}


	void* DefaultAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
	{
		// POSIX and glibc do not provide a way to realloc with alignment preservation
		if (size == 0) {
			free(ptr);
			return nullptr;
		}
		void* newptr = aligned_alloc(align, size);
		if (newptr == nullptr) {
			return nullptr;
		}
		memcpy(newptr, ptr, malloc_usable_size(ptr));
		free(ptr);
		return newptr;
	}
#endif

	
BaseProxyAllocator::BaseProxyAllocator(IAllocator& source)
	: m_source(source)
{
	m_allocation_count = 0;
}

BaseProxyAllocator::~BaseProxyAllocator() { ASSERT(m_allocation_count == 0); }


void* BaseProxyAllocator::allocate_aligned(size_t size, size_t align)
{
	atomicIncrement(&m_allocation_count);
	return m_source.allocate_aligned(size, align);
}


void BaseProxyAllocator::deallocate_aligned(void* ptr)
{
	if(ptr)
	{
		atomicDecrement(&m_allocation_count);
		m_source.deallocate_aligned(ptr);
	}
}


void* BaseProxyAllocator::reallocate_aligned(void* ptr, size_t size, size_t align)
{
	if (!ptr) atomicIncrement(&m_allocation_count);
	if (size == 0) atomicDecrement(&m_allocation_count);
	return m_source.reallocate_aligned(ptr, size, align);
}


void* BaseProxyAllocator::allocate(size_t size)
{
	atomicIncrement(&m_allocation_count);
	return m_source.allocate(size);
}

void BaseProxyAllocator::deallocate(void* ptr)
{
	if (ptr)
	{
		atomicDecrement(&m_allocation_count);
		m_source.deallocate(ptr);
	}
}

void* BaseProxyAllocator::reallocate(void* ptr, size_t size)
{
	if (!ptr) atomicIncrement(&m_allocation_count);
	if (size == 0) atomicDecrement(&m_allocation_count);
	return m_source.reallocate(ptr, size);
}


} // namespace Lumix

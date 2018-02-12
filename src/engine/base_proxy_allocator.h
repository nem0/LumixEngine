#pragma once


#include "engine/iallocator.h"
#include "engine/mt/atomic.h"


namespace Lumix
{


class BaseProxyAllocator : public IAllocator
{
public:
	explicit BaseProxyAllocator(IAllocator& source)
		: m_source(source)
	{
		m_allocation_count = 0;
	}

	~BaseProxyAllocator() { ASSERT(m_allocation_count == 0); }


	void* allocate_aligned(size_t size, size_t align) override
	{
		MT::atomicIncrement(&m_allocation_count);
		return m_source.allocate_aligned(size, align);
	}


	void deallocate_aligned(void* ptr) override
	{
		if(ptr)
		{
			MT::atomicDecrement(&m_allocation_count);
			m_source.deallocate_aligned(ptr);
		}
	}


	void* reallocate_aligned(void* ptr, size_t size, size_t align) override
	{
		if (!ptr) MT::atomicIncrement(&m_allocation_count);
		if (size == 0) MT::atomicDecrement(&m_allocation_count);
		return m_source.reallocate_aligned(ptr, size, align);
	}


	void* allocate(size_t size) override
	{
		MT::atomicIncrement(&m_allocation_count);
		return m_source.allocate(size);
	}

	void deallocate(void* ptr) override
	{
		if (ptr)
		{
			MT::atomicDecrement(&m_allocation_count);
			m_source.deallocate(ptr);
		}
	}

	void* reallocate(void* ptr, size_t size) override
	{
		if (!ptr) MT::atomicIncrement(&m_allocation_count);
		if (size == 0) MT::atomicDecrement(&m_allocation_count);
		return m_source.reallocate(ptr, size);
	}


	IAllocator& getSourceAllocator() { return m_source; }

private:
	IAllocator& m_source;
	volatile i32 m_allocation_count;
};


} // namespace Lumix

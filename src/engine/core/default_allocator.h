#pragma once


#include "engine/lumix.h"
#include "engine/core/iallocator.h"


namespace Lumix
{

class LUMIX_ENGINE_API DefaultAllocator : public IAllocator
{
public:
	DefaultAllocator() {}
	~DefaultAllocator() {}

	void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
};


} // ~namespace Lumix

#pragma once


#include "lumix.h"
#include "core/iallocator.h"


namespace Lumix
{

class LUMIX_ENGINE_API DefaultAllocator : public IAllocator
{
public:
	DefaultAllocator() {}
	~DefaultAllocator() {}

	virtual void* allocate(size_t n) override;
	void deallocate(void* p) override;
	void* reallocate(void* ptr, size_t size) override;
};


} // ~namespace Lumix

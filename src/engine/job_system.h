#pragma once


#include "engine/iallocator.h"


namespace Lumix
{


class Engine;


namespace JobSystem
{


struct LUMIX_ENGINE_API JobDecl
{
	void (*task)(void*);
	void* data;
};


LUMIX_ENGINE_API bool init(Engine& engine);
LUMIX_ENGINE_API void shutdown();
LUMIX_ENGINE_API void runJobs(const JobDecl* jobs, int count, int volatile* counter);
LUMIX_ENGINE_API void wait(int volatile* counter);
LUMIX_ENGINE_API void waitOutsideJob();


struct LUMIX_ENGINE_API LambdaJob : JobDecl
{
	LambdaJob() { data = pool; }
	~LambdaJob() { if (data != pool) allocator->deallocate(data); }
	u8 pool[64];
	IAllocator* allocator;
};


template <typename T> void lambdaInvoker(void* data)
{
	(*(T*)data)();
}


template<typename T>
void fromLambda(T lambda, LambdaJob* job, IAllocator* allocator)
{
	job->allocator = allocator;
	if (sizeof(lambda) <= sizeof(job->pool))
	{
		job->data = job->pool;
	}
	else
	{
		ASSERT(allocator);
		job->data = allocator->allocate(sizeof(T));
	}
	new (NewPlaceholder(), job->data) T(lambda);
	job->task = &lambdaInvoker<T>;
}


} // namespace JobSystem


} // namespace Lumix
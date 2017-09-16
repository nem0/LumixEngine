#pragma once


#include "engine/iallocator.h"


namespace Lumix
{


class Engine;


namespace JobSystem
{


struct JobDecl
{
	void (*task)(void*);
	void* data;
};


bool init(Engine& engine);
void shutdown();
void runJobs(const JobDecl* jobs, int count, int volatile* counter);
void wait(int volatile* counter);
void waitOutsideJob();


struct LambdaJob : JobDecl
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
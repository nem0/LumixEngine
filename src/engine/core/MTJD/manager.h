#pragma once


#define SINGLE_THREAD 0
#define MULTI_THREAD 1

#define TYPE MULTI_THREAD

#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/transaction.h"


namespace Lumix
{
namespace MTJD
{


class Job;
class WorkerTask;


class LUMIX_ENGINE_API Manager
{
	friend class Scheduler;
	friend class WorkerTask;

public:
	typedef MT::Transaction<Job*> JobTrans;
	typedef MT::LockFreeFixedQueue<JobTrans, 32> JobTransQueue;

	virtual ~Manager() {}

	virtual uint32 getCpuThreadsCount() const = 0;
	virtual void schedule(Job* job) = 0;
	virtual void doScheduling() = 0;

	static Manager* create(IAllocator& allocator);
	static void destroy(Manager& manager);
};


} // namepsace MTJD
} // namepsace Lumix

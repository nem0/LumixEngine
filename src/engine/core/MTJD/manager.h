#pragma once


#define SINGLE_THREAD	0
#define MULTI_THREAD	1

#define TYPE MULTI_THREAD

#include "core/array.h"
#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/transaction.h"
#include "core/mtjd/enums.h"
#include "core/mtjd/scheduler.h"


namespace Lumix
{
	namespace MTJD
	{
		class Job;
		class WorkerTask;

		class LUMIX_ENGINE_API Manager
		{
			friend class Scheduler;
			friend class SpuHelperTask;
			friend class WorkerTask;

		public:

			typedef MT::LockFreeFixedQueue<Job*, 512>	JobsTable;
			typedef MT::Transaction<Job*>				JobTrans;
			typedef MT::LockFreeFixedQueue<JobTrans, 32>  JobTransQueue;
			typedef Array<JobTrans*>					TransTable;

			Manager(IAllocator& allocator);
			~Manager();

			uint32 getCpuThreadsCount() const;

			void schedule(Job* job);

		private:
			void scheduleCpu(Job* job);

			void doScheduling();

			Job* getNextReadyJob();

			void pushReadyJob(Job* job);

			uint32 getAffinityMask(uint32 idx) const;

			IAllocator&		m_allocator;
			JobsTable		m_ready_to_execute[(size_t)Priority::Count];
			JobTransQueue	m_trans_queue;
			TransTable		m_pending_trans;
			Array<WorkerTask*> m_worker_tasks;
			Scheduler		m_scheduler;

			volatile int32 m_scheduling_counter;
		};
	} // namepsace MTJD
} // namepsace Lumix

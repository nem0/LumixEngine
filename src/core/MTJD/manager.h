#pragma once


#define SINGLE_THREAD	0
#define MULTI_THREAD	1

#define TYPE MULTI_THREAD

#include "core/MT/lock_free_queue.h"
#include "core/MTJD/enums.h"
#include "core/MTJD/scheduler.h"
#include "core/MT/Task.h"
#include "core/MT/transaction_queue.h"
#include "core/Array.h"

namespace Lux
{
	namespace MTJD
	{
		class Job;
		class WorkerTask;

		class LUX_CORE_API Manager
		{
			friend class Scheduler;
			friend class SpuHelperTask;
			friend class WorkerTask;

		public:

			typedef MT::LockFreeQueue<Job, 512>		JobsTable;
			typedef MT::Transaction<Job*>			JobTrans;
			typedef MT::TransactionQueue<JobTrans, 32>  JobTransQueue;
			typedef Array<JobTrans*>					TransTable;

			Manager();
			~Manager();

			uint32_t getCpuThreadsCount() const;

			void schedule(Job* job);

		private:
			void scheduleCpu(Job* job);

			void doScheduling();

			Job* getNextReadyJob();

			void pushFrontReadyJob(Job* job);
			void pushBackReadyJob(Job* job);

			uint32_t getAffinityMask(uint32_t idx) const;

			JobsTable		m_ready_to_execute[Priority::Count];
			JobTransQueue	m_trans_queue;
			TransTable		m_pending_trans;
			WorkerTask*		m_worker_tasks;
			Scheduler		m_scheduler;

			volatile int32_t m_scheduling_counter;
		};
	} // ~namepsace MTJD
} // ~namepsace Lux
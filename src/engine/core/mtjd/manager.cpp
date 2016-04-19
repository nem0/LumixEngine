#include "lumix.h"
#include "core/mtjd/manager.h"

#include "core/mtjd/job.h"
#include "core/mtjd/scheduler.h"
#include "core/mtjd/worker_thread.h"

#include "core/mt/thread.h"

namespace Lumix
{
namespace MTJD
{


struct ManagerImpl : public Manager
{
	typedef MT::LockFreeFixedQueue<Job*, 512>	JobsTable;
	typedef Array<JobTrans*>					TransTable;


	ManagerImpl(IAllocator& allocator)
		: m_scheduling_counter(0)
		, m_scheduler(*this, allocator)
		, m_worker_tasks(allocator)
		, m_allocator(allocator)
		, m_pending_trans(allocator)
	{
#if TYPE == MULTI_THREAD
		uint32 threads_num = getCpuThreadsCount();

		m_scheduler.create("MTJD::Scheduler");
		m_scheduler.run();

		m_worker_tasks.reserve(threads_num);
		for (uint32 i = 0; i < threads_num; ++i)
		{
			m_worker_tasks.push(LUMIX_NEW(m_allocator, WorkerTask)(m_allocator));
			m_worker_tasks[i]->create("MTJD::WorkerTask", this, &m_trans_queue);
			m_worker_tasks[i]->setAffinityMask(getAffinityMask(i));
			m_worker_tasks[i]->run();
		}

#endif // TYPE == MULTI_THREAD
	}

	~ManagerImpl()
	{
#if TYPE == MULTI_THREAD

		uint32 threads_num = getCpuThreadsCount();
		for (uint32 i = 0; i < threads_num; ++i)
		{
			m_trans_queue.abort();
		}

		for (uint32 i = 0; i < threads_num; ++i)
		{
			m_worker_tasks[i]->destroy();
			LUMIX_DELETE(m_allocator, m_worker_tasks[i]);
		}

		m_scheduler.forceExit(false);
		m_scheduler.dataSignal();
		m_scheduler.destroy();

#endif // TYPE == MULTI_THREAD
	}

	uint32 getCpuThreadsCount() const override
	{
#if TYPE == MULTI_THREAD

		return MT::getCPUsCount() <= 1 ? 1 : MT::getCPUsCount() - 1; // -1 for bgfx thread

#else // TYPE == MULTI_THREAD

		return 1;

#endif // TYPE == MULTI_THREAD
	}


	void schedule(Job* job) override
	{
		ASSERT(job);
		ASSERT(false == job->m_scheduled);
		ASSERT(job->m_dependency_count > 0);

#if TYPE == MULTI_THREAD

		if (1 == job->getDependenceCount())
		{
			job->m_scheduled = true;

			pushReadyJob(job);

			m_scheduler.dataSignal();
		}

#else // TYPE == MULTI_THREAD

		job->execute();
		job->onExecuted();

#endif // TYPE == MULTI_THREAD
	}

	void scheduleCpu(Job* job)
	{
		JobTrans* tr = m_trans_queue.alloc(false);
		if (tr)
		{
			tr->data = job;
			if (!m_trans_queue.push(tr, false))
			{
				m_trans_queue.dealoc(tr);
				pushReadyJob(job);
			}
			else
			{
				m_pending_trans.push(tr);
			}
		}
	}

	void doScheduling()
	{
#if TYPE == MULTI_THREAD

		uint32 count = MT::atomicIncrement(&m_scheduling_counter);
		if (1 == count)
		{
			do
			{
				for (int i = 0; i < m_pending_trans.size();)
				{
					JobTrans* tr = m_pending_trans[i];
					if (tr->isCompleted())
					{
						tr->data->onExecuted();
						m_trans_queue.dealoc(tr);
						m_pending_trans.eraseFast(i);
					}
					else
					{
						++i;
					}
				}

				Job* job = getNextReadyJob();
				if (job)
				{
					scheduleCpu(job);
				}

				count = MT::atomicDecrement(&m_scheduling_counter);
			} while (0 < count);
		}

#endif // TYPE == MULTI_THREAD
	}

	Job* getNextReadyJob()
	{
#if TYPE == MULTI_THREAD

		for (int32 i = 0; i < (int32)Priority::Count; ++i)
		{
			if (!m_ready_to_execute[i].isEmpty())
			{
				Job** entry = m_ready_to_execute[i].pop(true);
				Job* ret = *entry;
				m_ready_to_execute[i].dealoc(entry);

				return ret;
			}
		}

#endif // TYPE == MULTI_THREAD

		return nullptr;
	}

	void pushReadyJob(Job* job)
	{
		ASSERT(job);

#if TYPE == MULTI_THREAD

		Job** jobEntry = m_ready_to_execute[(int32)job->getPriority()].alloc(true);
		if (jobEntry)
		{
			*jobEntry = job;
			m_ready_to_execute[(int32)job->getPriority()].push(jobEntry, true);
		}

#endif // TYPE == MULTI_THREAD
	}

	uint32 getAffinityMask(uint32) const
	{
#if defined(_WIN32) || defined(_WIN64)
		return MT::getProccessAffinityMask();
#else
#error "Not Supported!"
#endif
	}

	IAllocator&			m_allocator;
	JobsTable			m_ready_to_execute[(size_t)Priority::Count];
	JobTransQueue		m_trans_queue;
	TransTable			m_pending_trans;
	Array<WorkerTask*>	m_worker_tasks;
	Scheduler			m_scheduler;

	volatile int32 m_scheduling_counter;


}; // struct ManagerImpl


Manager* Manager::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, ManagerImpl)(allocator);
}


void Manager::destroy(Manager& manager)
{
	LUMIX_DELETE(static_cast<ManagerImpl&>(manager).m_allocator, &manager);
}


} // namepsace MTJD
} // namepsace Lumix

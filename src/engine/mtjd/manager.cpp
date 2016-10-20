#include "engine/lumix.h"
#include "engine/mtjd/manager.h"

#include "engine/mtjd/job.h"
#include "engine/mtjd/scheduler.h"
#include "engine/mtjd/worker_thread.h"

#include "engine/mt/thread.h"

namespace Lumix
{
namespace MTJD
{


struct ManagerImpl LUMIX_FINAL : public Manager
{
	typedef MT::LockFreeFixedQueue<Job*, 512>	JobsTable;
	typedef Array<JobTrans*>					TransTable;


	ManagerImpl(IAllocator& allocator)
		: m_scheduling_counter(0)
		#if !LUMIX_SINGLE_THREAD()
			, m_scheduler(*this, allocator)
		#endif
		, m_worker_tasks(allocator)
		, m_allocator(allocator)
		, m_pending_trans(allocator)
	{
#if !LUMIX_SINGLE_THREAD()
		uint32 threads_num = getCpuThreadsCount();

		m_scheduler.create("MTJD::Scheduler");

		m_worker_tasks.reserve(threads_num);
		for (uint32 i = 0; i < threads_num; ++i)
		{
			auto& task = m_worker_tasks.emplace(m_allocator);
			task.create("MTJD::WorkerTask", this, &m_trans_queue);
			task.setAffinityMask(getAffinityMask(i));
		}

#endif
	}

	~ManagerImpl()
	{
#if !LUMIX_SINGLE_THREAD()

		uint32 threads_num = getCpuThreadsCount();
		for (uint32 i = 0; i < threads_num; ++i)
		{
			m_trans_queue.abort();
		}

		for (auto& task : m_worker_tasks)
		{
			task.destroy();
		}

		m_scheduler.forceExit(false);
		m_scheduler.dataSignal();
		m_scheduler.destroy();

#endif
	}

	uint32 getCpuThreadsCount() const override
	{
#if !LUMIX_SINGLE_THREAD()

		return MT::getCPUsCount() <= 1 ? 1 : MT::getCPUsCount() - 1; // -1 for bgfx thread

#else

		return 1;

#endif
	}


	void schedule(Job* job) override
	{
		ASSERT(job);
		ASSERT(false == job->m_scheduled);
		ASSERT(job->m_dependency_count > 0);

#if !LUMIX_SINGLE_THREAD()

		if (1 == job->getDependenceCount())
		{
			job->m_scheduled = true;

			pushReadyJob(job);

			m_scheduler.dataSignal();
		}

#else

		job->execute();
		job->onExecuted();

#endif
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

	void doScheduling() override
	{
#if !LUMIX_SINGLE_THREAD()

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

#endif
	}

	Job* getNextReadyJob()
	{
#if !LUMIX_SINGLE_THREAD()

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

#endif

		return nullptr;
	}

	void pushReadyJob(Job* job)
	{
		ASSERT(job);

#if !LUMIX_SINGLE_THREAD()

		Job** jobEntry = m_ready_to_execute[(int32)job->getPriority()].alloc(true);
		if (jobEntry)
		{
			*jobEntry = job;
			m_ready_to_execute[(int32)job->getPriority()].push(jobEntry, true);
		}

#endif
	}

	uint32 getAffinityMask(uint32) const
	{
		return MT::getThreadAffinityMask();
	}

	IAllocator&			m_allocator;
	JobsTable			m_ready_to_execute[(size_t)Priority::Count];
	JobTransQueue		m_trans_queue;
	TransTable			m_pending_trans;
	Array<WorkerTask>	m_worker_tasks;
	#if !LUMIX_SINGLE_THREAD()
		Scheduler			m_scheduler;
	#endif

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

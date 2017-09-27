#include "job_system.h"
#include "engine/array.h"
#include "engine/engine.h"
#include "engine/fibers.h"
#include "engine/iallocator.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"

namespace Lumix
{


namespace JobSystem
{


struct Job
{
	JobDecl decl;
	volatile int* counter;
};


struct FiberDecl
{
	int idx;
	Fiber::Handle fiber;
	Job current_job;
	struct WorkerTask* worker_task;
	void* switch_state;
};


struct SleepingFiber
{
	volatile int* waiting_condition;
	FiberDecl* fiber;
};



struct System
{
	System(IAllocator& allocator) 
		: m_allocator(allocator)
		, m_workers(allocator)
		, m_job_queue(allocator)
		, m_sleeping_fibers(allocator)
		, m_sync(false)
		, m_work_signal(true)
	{}


	MT::SpinMutex m_sync;
	MT::Event m_work_signal;
	Array<MT::Task*> m_workers;
	Array<Job> m_job_queue;
	FiberDecl m_fiber_pool[256];
	int m_free_fibers_indices[256];
	int m_num_free_fibers;
	volatile int m_job_count;
	Array<SleepingFiber> m_sleeping_fibers;
	IAllocator& m_allocator;
};


static System* g_system = nullptr;


static bool getReadySleepingFiber(System& system, SleepingFiber* out)
{
	MT::SpinLock lock(system.m_sync);

	int count = system.m_sleeping_fibers.size();
	for (int i = 0; i < count; ++i)
	{
		SleepingFiber job = system.m_sleeping_fibers[i];
		if (*job.waiting_condition <= 0)
		{
			system.m_sleeping_fibers.eraseFast(i);
			*out = job;
			return true;
		}
	}
	return false;
}


static bool getReadyJob(System& system, Job* out)
{
	MT::SpinLock lock(system.m_sync);

	if (system.m_job_queue.empty()) return false;

	Job job = system.m_job_queue.back();
	system.m_job_queue.pop();
	if (system.m_job_queue.empty()) system.m_work_signal.reset();
	*out = job;

	return true;
}


struct WorkerTask : MT::Task
{
	WorkerTask(System& system) 
		: Task(system.m_allocator)
		, m_system(system) 
	{}


	static FiberDecl& getFreeFiber()
	{
		MT::SpinLock lock(g_system->m_sync);
		
		ASSERT(g_system->m_num_free_fibers > 0);
		--g_system->m_num_free_fibers;
		int free_fiber_idx = g_system->m_free_fibers_indices[g_system->m_num_free_fibers];

		return g_system->m_fiber_pool[free_fiber_idx];
	}


	static void handleSwitch(FiberDecl& fiber)
	{
		MT::SpinLock lock(g_system->m_sync);

		if (!fiber.switch_state)
		{
			g_system->m_free_fibers_indices[g_system->m_num_free_fibers] = fiber.idx;
			++g_system->m_num_free_fibers;
			return;
		}

		volatile int* counter = (volatile int*)fiber.switch_state;
		SleepingFiber sleeping_fiber;
		sleeping_fiber.fiber = &fiber;
		sleeping_fiber.waiting_condition = counter;

		g_system->m_sleeping_fibers.push(sleeping_fiber);
	}


	int task() override
	{
		m_primary_fiber = Fiber::createFromThread(this);
		manage();
		return 0;
	}


	void manage()
	{
		m_finished = false;
		while (!m_finished)
		{
			SleepingFiber ready_sleeping_fiber;
			if (getReadySleepingFiber(*g_system, &ready_sleeping_fiber))
			{
				ready_sleeping_fiber.fiber->worker_task = this;
				ready_sleeping_fiber.fiber->switch_state = nullptr;
				PROFILE_BLOCK("work");
				Fiber::switchTo(ready_sleeping_fiber.fiber->fiber);
				handleSwitch(*ready_sleeping_fiber.fiber);
				continue;
			}

			Job job;
			if (getReadyJob(*g_system, &job))
			{
				FiberDecl& fiber_decl = getFreeFiber();
				fiber_decl.worker_task = this;
				fiber_decl.current_job = job;
				fiber_decl.switch_state = nullptr;
				PROFILE_BLOCK("work");
				Fiber::switchTo(fiber_decl.fiber);
				handleSwitch(fiber_decl);
			}
			else 
			{
				PROFILE_BLOCK("wait");
				g_system->m_work_signal.waitTimeout(1);
			}
		}
	}


	bool m_finished;
	Fiber::Handle m_primary_fiber;
	System& m_system;
};


#ifdef _WIN32
static void __stdcall fiberProc(void* data)
#else
static void fiberProc(void* data)
#endif
{
	FiberDecl* fiber_decl = (FiberDecl*)data;
	for (;;)
	{
		Job job = fiber_decl->current_job;
		job.decl.task(job.decl.data);
		if(job.counter) MT::atomicDecrement(job.counter);

		fiber_decl->switch_state = nullptr;
		Fiber::switchTo(fiber_decl->worker_task->m_primary_fiber);
	}
}


bool init(IAllocator& allocator)
{
	ASSERT(!g_system);

	g_system = LUMIX_NEW(allocator, System)(allocator);
	g_system->m_work_signal.reset();

	int count = Math::maximum(1, int(MT::getCPUsCount() - 1));
	for (int i = 0; i < count; ++i)
	{
		WorkerTask* task = LUMIX_NEW(allocator, WorkerTask)(*g_system);
		if (task->create("Job system worker"))
		{
			g_system->m_workers.push(task);
			task->setAffinityMask((u64)1 << i);
		}
		else
		{
			g_log_error.log("Engine") << "Job system worker failed to initialize.";
			LUMIX_DELETE(allocator, task);
		}
	}

	int fiber_num = lengthOf(g_system->m_fiber_pool);
	g_system->m_num_free_fibers = fiber_num;
	for(int i = 0; i < fiber_num; ++i)
	{ 
		FiberDecl& decl = g_system->m_fiber_pool[i];
		decl.fiber = Fiber::create(64 * 1024, fiberProc, &g_system->m_fiber_pool[i]);
		decl.idx = i;
		decl.worker_task = nullptr;
		g_system->m_free_fibers_indices[i] = i;
	}

	return !g_system->m_workers.empty();
}


void shutdown()
{
	if (!g_system) return;

	IAllocator& allocator = g_system->m_allocator;
	for (MT::Task* task : g_system->m_workers)
	{
		WorkerTask* wt = (WorkerTask*)task;
		wt->m_finished = true;
	}

	for (MT::Task* task : g_system->m_workers)
	{
		while (!task->isFinished()) g_system->m_work_signal.trigger();
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (FiberDecl& fiber : g_system->m_fiber_pool)
	{
		Fiber::destroy(fiber.fiber);
	}

	LUMIX_DELETE(allocator, g_system);
	g_system = nullptr;
}


void runJobs(const JobDecl* jobs, int count, int volatile* counter)
{
	ASSERT(g_system);
	ASSERT(count > 0);

	MT::SpinLock lock(g_system->m_sync);
	g_system->m_work_signal.trigger();
	if (counter) MT::atomicAdd(counter, count);
	for (int i = 0; i < count; ++i)
	{
		Job job;
		job.decl = jobs[i];
		job.counter = counter;
		g_system->m_job_queue.push(job);
	}
}


void wait(int volatile* counter)
{
	FiberDecl* fiber_decl = (FiberDecl*)Fiber::getParameter();
	fiber_decl->switch_state = (void*)counter;
	Fiber::switchTo(fiber_decl->worker_task->m_primary_fiber);
}



void waitOutsideJob(volatile int* counter)
{
	PROFILE_FUNCTION();
	while (*counter != 0) MT::yield();
}


} // namespace JobSystem


} // namespace Lumix
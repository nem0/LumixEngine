#include "job_system.h"
#include "engine/array.h"
#include "engine/engine.h"
#include "engine/fibers.h"
#include "engine/iallocator.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/mt/atomic.h"
#include "engine/mt/lock_free_fixed_queue.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include <malloc.h> // TODO

namespace Lumix
{


namespace JobSystem
{

enum { 
	HANDLE_ID_MASK = 0xffFF,
	HANDLE_GENERATION_MASK = 0xffFF0000 
};


struct Job
{
	void (*task)(void*) = nullptr;
	void* data = nullptr;
	SignalHandle dec_on_finish;
	SignalHandle precondition;
	u8 worker_index;
};


struct Signal {
	volatile int value;
	u32 generation;
	Job next_job;
	SignalHandle sibling;
};


struct WorkerTask;


struct FiberDecl
{
	int idx;
	Fiber::Handle fiber;
	Job current_job;
	bool job_finished;
	WorkerTask* worker_task;
};


struct System
{
	System(IAllocator& allocator) 
		: m_allocator(allocator)
		, m_workers(allocator)
		, m_job_queue(allocator)
		, m_ready_fibers(allocator)
		, m_signals_pool(allocator)
		, m_work_signal(true)
		, m_event_outside_job(true)
		, m_free_queue(allocator)
		, m_free_fibers(allocator)
	{
		m_signals_pool.resize(4096);
		m_free_queue.resize(4096);
		m_event_outside_job.trigger();
		m_work_signal.reset();
		for(int i = 0; i < 4096; ++i) {
			m_free_queue[i] = i;
			m_signals_pool[i].sibling = JobSystem::INVALID_HANDLE;
			m_signals_pool[i].generation = 0;
		}
	}


	MT::CriticalSection m_sync;
	MT::CriticalSection m_ready_fiber_sync;
	MT::CriticalSection m_job_queue_sync;
	MT::Event m_event_outside_job;
	MT::Event m_work_signal;
	Array<WorkerTask*> m_workers;
	Array<Job> m_job_queue;
	Array<Signal> m_signals_pool;
	FiberDecl m_fiber_pool[512];
	Array<FiberDecl*> m_free_fibers;
	Array<FiberDecl*> m_ready_fibers;
	IAllocator& m_allocator;
	Array<u32> m_free_queue;
};


static System* g_system = nullptr;
static thread_local WorkerTask* g_worker = nullptr;


struct WorkerTask : MT::Task
{
	WorkerTask(System& system, u8 worker_index) 
		: Task(system.m_allocator)
		, m_system(system)
		, m_worker_index(worker_index)
		, m_job_queue(system.m_allocator)
		, m_ready_fibers(system.m_allocator)
	{}


	Job getReadyJob(System& system)
	{
		MT::CriticalSectionLock lock(system.m_job_queue_sync);

		if (!m_job_queue.empty()) {
			const Job job = m_job_queue.back();
			m_job_queue.pop();
			return job;
		}

		if (system.m_job_queue.empty()) return { nullptr, nullptr };

		const Job job = system.m_job_queue.back();
		system.m_job_queue.pop();
		if (system.m_job_queue.empty()) system.m_work_signal.reset();
		return job;
	}


	FiberDecl* getReadyFiber(System& system)
	{
		MT::CriticalSectionLock lock(system.m_ready_fiber_sync);

		if (!m_ready_fibers.empty()) {
			FiberDecl* fiber = m_ready_fibers.back();
			m_ready_fibers.pop();
			return fiber;
		}

		if (system.m_ready_fibers.empty()) return nullptr;
		FiberDecl* fiber = system.m_ready_fibers.back();
		system.m_ready_fibers.pop();
		return fiber;
	}


	static FiberDecl& getFreeFiber()
	{
		MT::CriticalSectionLock lock(g_system->m_sync);
		
		LUMIX_FATAL(!g_system->m_free_fibers.empty());
		FiberDecl* decl = g_system->m_free_fibers.back();
		g_system->m_free_fibers.pop();

		return *decl;
	}


	static void handleSwitch(FiberDecl& fiber)
	{
		if (fiber.job_finished) {
			g_system->m_free_fibers.push(&fiber);
		}
		g_system->m_sync.exit();
	}


	int task() override
	{
		g_worker = this;
		Fiber::initThread(manage, &m_primary_fiber);
		return 0;
	}


	#ifdef _WIN32
		static void __stdcall manage(void* data)
	#else
		static void manage(void* data)
	#endif
	{
		WorkerTask* that = g_worker;
		that->m_finished = false;
		Profiler::beginBlock("job management");
		Profiler::blockColor(0, 0, 0xff);
		
		while (!that->m_finished)
		{
			FiberDecl* fiber = that->getReadyFiber(*g_system);
			if (fiber) {
				fiber->worker_task = that;
				that->m_current_fiber = fiber;
				fiber->job_finished = false;

				g_system->m_sync.enter();

				Profiler::endBlock();
				Fiber::switchTo(&that->m_primary_fiber, fiber->fiber);
				Profiler::beginBlock("job management");
				Profiler::blockColor(0, 0, 0xff);

				that->m_current_fiber = nullptr;
				handleSwitch(*fiber);
				continue;
			}

			Job job = that->getReadyJob(*g_system);
			if (job.task) {
				FiberDecl& fiber_decl = getFreeFiber();
				fiber_decl.worker_task = that;
				fiber_decl.current_job = job;
				fiber_decl.job_finished = false;
				that->m_current_fiber = &fiber_decl;

				g_system->m_sync.enter();

				Profiler::endBlock();
				Fiber::switchTo(&that->m_primary_fiber, fiber_decl.fiber);
				Profiler::beginBlock("job management");
				Profiler::blockColor(0, 0, 0xff);

				that->m_current_fiber = nullptr;
				handleSwitch(fiber_decl);
			}
			else 
			{
				PROFILE_BLOCK("idle");
				Profiler::blockColor(0xff, 0, 0xff);
				g_system->m_work_signal.waitTimeout(1);
			}
		}
		Profiler::endBlock();
	}


	bool m_finished = false;
	FiberDecl* m_current_fiber = nullptr;
	Fiber::Handle m_primary_fiber;
	System& m_system;
	Array<Job> m_job_queue;
	Array<FiberDecl*> m_ready_fibers;
	u8 m_worker_index;
};


static LUMIX_FORCE_INLINE SignalHandle allocateSignal()
{
	LUMIX_FATAL(!g_system->m_free_queue.empty());

	const u32 handle = g_system->m_free_queue.back();
	Signal& w = g_system->m_signals_pool[handle & HANDLE_ID_MASK];
	w.value = 1;
	w.sibling = JobSystem::INVALID_HANDLE;
	w.next_job.task = nullptr;
	g_system->m_free_queue.pop();

	return handle & HANDLE_ID_MASK | w.generation;
}


static void pushJob(const Job& job)
{
	if (job.worker_index != ANY_WORKER) {
		g_system->m_workers[job.worker_index % g_system->m_workers.size()]->m_job_queue.push(job);
		return;
	}
	g_system->m_job_queue.push(job);
}


void trigger(SignalHandle handle)
{
	LUMIX_FATAL((handle & HANDLE_ID_MASK) < 4096);

	MT::CriticalSectionLock lock(g_system->m_sync);
	
	Signal& counter = g_system->m_signals_pool[handle & HANDLE_ID_MASK];
	--counter.value;
	if (counter.value > 0) return;

	bool any_new_job = false;
	SignalHandle iter = handle;
	while (isValid(iter)) {
		Signal& signal = g_system->m_signals_pool[iter & HANDLE_ID_MASK];
		if(signal.next_job.task) {
			MT::CriticalSectionLock lock(g_system->m_job_queue_sync);
			pushJob(signal.next_job);
			any_new_job = true;
		}
		signal.generation = (((signal.generation >> 16) + 1) & 0xffFF) << 16;
		g_system->m_free_queue.push(iter & HANDLE_ID_MASK | signal.generation);
		signal.next_job.task = nullptr;
		iter = signal.sibling;
	}
	if (any_new_job) {
		MT::CriticalSectionLock lock(g_system->m_job_queue_sync);
		if (!g_system->m_job_queue.empty()) {
			g_system->m_work_signal.trigger();
		}
	}
}


static LUMIX_FORCE_INLINE bool isSignalZero(SignalHandle handle, bool lock)
{
	if (!isValid(handle)) return true;

	const u32 gen = handle & HANDLE_GENERATION_MASK;
	const u32 id = handle & HANDLE_ID_MASK;
	
	if (lock) g_system->m_sync.enter();
	Signal& counter = g_system->m_signals_pool[id];
	bool is_zero = counter.generation != gen || counter.value == 0;
	if (lock) g_system->m_sync.exit();
	return is_zero;
}


static LUMIX_FORCE_INLINE void runInternal(void* data
	, void (*task)(void*)
	, SignalHandle precondition
	, bool lock
	, SignalHandle* on_finish
	, u8 worker_index)
{
	Job j;
	j.data = data;
	j.task = task;
	j.worker_index = worker_index;
	j.precondition = precondition;

	if (lock) g_system->m_sync.enter();
	j.dec_on_finish = [&]() -> SignalHandle {
		if (!on_finish) return INVALID_HANDLE;
		if (isValid(*on_finish) && !isSignalZero(*on_finish, false)) {
			++g_system->m_signals_pool[*on_finish & HANDLE_ID_MASK].value;
			return *on_finish;
		}
		return allocateSignal();
	}();
	if (on_finish) *on_finish = j.dec_on_finish;

	if (!isValid(precondition) || isSignalZero(precondition, false)) {
		MT::CriticalSectionLock lock(g_system->m_job_queue_sync);
		pushJob(j);
		g_system->m_work_signal.trigger();
	}
	else {
		Signal& counter = g_system->m_signals_pool[precondition & HANDLE_ID_MASK];
		if(counter.next_job.task) {
			const SignalHandle ch = allocateSignal();
			Signal& c = g_system->m_signals_pool[ch & HANDLE_ID_MASK];
			c.next_job = j;
			c.sibling = counter.sibling;
			counter.sibling = ch;
		}
		else {
			counter.next_job = j;
		}
	}

	if (lock) g_system->m_sync.exit();
}


void incSignal(SignalHandle* signal)
{
	ASSERT(signal);
	MT::CriticalSectionLock lock(g_system->m_sync);
	
	if (isValid(*signal) && !isSignalZero(*signal, false)) {
		++g_system->m_signals_pool[*signal & HANDLE_ID_MASK].value;
	}
	else {
		*signal = allocateSignal();
	}
}


void decSignal(SignalHandle signal)
{
	trigger(signal);
}


void run(void* data, void(*task)(void*), SignalHandle* on_finished)
{
	runInternal(data, task, INVALID_HANDLE, true, on_finished, ANY_WORKER);
}


void runEx(void* data, void(*task)(void*), SignalHandle* on_finished, SignalHandle precondition, u8 worker_index)
{
	runInternal(data, task, precondition, true, on_finished, worker_index);
}


#ifdef _WIN32
static void __stdcall fiberProc(void* data)
#else
static void fiberProc(void* data)
#endif
{
	g_system->m_sync.exit();

	FiberDecl* fiber_decl = (FiberDecl*)data;
	for (;;)
	{
		Job job = fiber_decl->current_job;
		Profiler::beginBlock("Job");
		if (isValid(job.dec_on_finish) || isValid(job.precondition)) {
			Profiler::pushJobInfo(job.dec_on_finish, job.precondition);
		}
		job.task(job.data);
		if (isValid(job.dec_on_finish)) {
			trigger(job.dec_on_finish);
		}
		fiber_decl->job_finished = true;
		Profiler::endBlock();

		g_system->m_sync.enter();
		Fiber::switchTo(&fiber_decl->fiber, fiber_decl->worker_task->m_primary_fiber);
		g_system->m_sync.exit();
	}
}


bool init(IAllocator& allocator)
{
	ASSERT(!g_system);

	g_system = LUMIX_NEW(allocator, System)(allocator);
	g_system->m_work_signal.reset();

	int count = Math::maximum(1, int(MT::getCPUsCount()));
	for (int i = 0; i < count; ++i) {
		WorkerTask* task = LUMIX_NEW(allocator, WorkerTask)(*g_system, i < 64 ? u64(1) << i : 0);
		if (task->create("Job system worker")) {
			g_system->m_workers.push(task);
			task->setAffinityMask((u64)1 << i);
		}
		else {
			g_log_error.log("Engine") << "Job system worker failed to initialize.";
			LUMIX_DELETE(allocator, task);
		}
	}

	g_system->m_free_fibers.reserve(lengthOf(g_system->m_fiber_pool));
	for (FiberDecl& fiber : g_system->m_fiber_pool) {
		g_system->m_free_fibers.push(&fiber);
	}

	const int fiber_num = lengthOf(g_system->m_fiber_pool);
	for(int i = 0; i < fiber_num; ++i) { 
		FiberDecl& decl = g_system->m_fiber_pool[i];
		decl.fiber = Fiber::create(64 * 1024, fiberProc, &g_system->m_fiber_pool[i]);
		decl.idx = i;
		decl.worker_task = nullptr;
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


void wait(SignalHandle handle)
{
	g_system->m_sync.enter();
	if (isSignalZero(handle, false)) {
		g_system->m_sync.exit();
		return;
	}
	
	if (g_worker) {
		PROFILE_BLOCK("waiting");
		Profiler::blockColor(0xff, 0, 0);
		FiberDecl* fiber_decl = g_worker->m_current_fiber;

		runInternal(fiber_decl, [](void* data){
			MT::CriticalSectionLock lock(g_system->m_ready_fiber_sync);
			FiberDecl* fiber = (FiberDecl*)data;
			if (fiber->current_job.worker_index == ANY_WORKER) {
				g_system->m_ready_fibers.push(fiber);
			}
			else {
				g_system->m_workers[fiber->current_job.worker_index % g_system->m_workers.size()]->m_ready_fibers.push(fiber);
			}
		}, handle, false, nullptr, 0);
		fiber_decl->job_finished = false;
		
		const int open_size = Profiler::getOpenBlocksSize();
		void* mem = _alloca(open_size);
		const i32 wait_id = Profiler::beginFiberWait(handle, mem);
		Fiber::switchTo(&fiber_decl->fiber, fiber_decl->worker_task->m_primary_fiber);
		Profiler::endFiberWait(handle, wait_id, mem);

		ASSERT(isSignalZero(handle, false));
		g_system->m_sync.exit();
	}
	else
	{
		// TODO maybe handle thi externally since main thread is no more
		PROFILE_BLOCK("not a job waiting");
		Profiler::blockColor(0xff, 0, 0);

		g_system->m_event_outside_job.reset();

		runInternal(nullptr, [](void* data) {
			g_system->m_event_outside_job.trigger();
		}, handle, false, nullptr, 0);

		g_system->m_sync.exit();

		MT::yield();
		while (!isSignalZero(handle, true)) {
			g_system->m_event_outside_job.waitTimeout(100);
		}
	}
}


} // namespace JobSystem


} // namespace Lumix
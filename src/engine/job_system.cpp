#include "engine/mt/atomic.h"
#include "job_system.h"
#include "engine/array.h"
#include "engine/engine.h"
#include "engine/fibers.h"
#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"


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
	Fiber::Handle fiber = Fiber::INVALID_FIBER;
	Job current_job;
};

#ifdef _WIN32
	static void __stdcall manage(void* data);
#else
	static void manage(void* data);
#endif

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
		, m_backup_workers(allocator)
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
	MT::CriticalSection m_job_queue_sync;
	MT::Event m_event_outside_job;
	MT::Event m_work_signal;
	Array<WorkerTask*> m_workers;
	Array<WorkerTask*> m_backup_workers;
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

#pragma optimize( "", off )
WorkerTask* getWorker()
{
	return g_worker;
}
#pragma optimize( "", on )


struct WorkerTask : MT::Task
{
	WorkerTask(System& system, u8 worker_index) 
		: Task(system.m_allocator)
		, m_system(system)
		, m_worker_index(worker_index)
		, m_job_queue(system.m_allocator)
		, m_ready_fibers(system.m_allocator)
		, m_enabled(true)
		, m_work_signal(true)
	{
		m_enabled.reset();
		m_work_signal.reset();
	}


	int task() override
	{
		Profiler::showInProfiler(true);
		g_worker = this;
		Fiber::initThread(start, &m_primary_fiber);
		return 0;
	}


	#ifdef _WIN32
		static void __stdcall start(void* data)
	#else
		static void start(void* data)
	#endif
	{
		g_system->m_sync.enter();
		FiberDecl* fiber = g_system->m_free_fibers.back();
		g_system->m_free_fibers.pop();
		if (fiber->fiber == Fiber::INVALID_FIBER) {
			fiber->fiber = Fiber::create(64 * 1024, manage, fiber);
		}
		getWorker()->m_current_fiber = fiber;
		Fiber::switchTo(&getWorker()->m_primary_fiber, fiber->fiber);
	}


	bool m_finished = false;
	FiberDecl* m_current_fiber = nullptr;
	Fiber::Handle m_primary_fiber;
	System& m_system;
	Array<Job> m_job_queue;
	Array<FiberDecl*> m_ready_fibers;
	u8 m_worker_index;
	bool m_is_enabled = false;
	bool m_is_backup = false;
	MT::Event m_enabled;
	MT::Event m_work_signal;
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

	return (handle & HANDLE_ID_MASK) | w.generation;
}


static void pushJob(const Job& job)
{
	if (job.worker_index != ANY_WORKER) {
		WorkerTask* worker = g_system->m_workers[job.worker_index % g_system->m_workers.size()];
		worker->m_job_queue.push(job);
		worker->m_work_signal.trigger();
		return;
	}
	g_system->m_job_queue.push(job);
	g_system->m_work_signal.trigger();
}


void trigger(SignalHandle handle)
{
	LUMIX_FATAL((handle & HANDLE_ID_MASK) < 4096);

	MT::CriticalSectionLock lock(g_system->m_sync);
	
	Signal& counter = g_system->m_signals_pool[handle & HANDLE_ID_MASK];
	--counter.value;
	if (counter.value > 0) return;

	SignalHandle iter = handle;
	while (isValid(iter)) {
		Signal& signal = g_system->m_signals_pool[iter & HANDLE_ID_MASK];
		if(signal.next_job.task) {
			MT::CriticalSectionLock queue_lock(g_system->m_job_queue_sync);
			pushJob(signal.next_job);
		}
		signal.generation = (((signal.generation >> 16) + 1) & 0xffFF) << 16;
		g_system->m_free_queue.push((iter & HANDLE_ID_MASK) | signal.generation);
		signal.next_job.task = nullptr;
		iter = signal.sibling;
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
	, bool do_lock
	, SignalHandle* on_finish
	, u8 worker_index)
{
	Job j;
	j.data = data;
	j.task = task;
	j.worker_index = worker_index != ANY_WORKER ? worker_index % getWorkersCount() : worker_index;
	j.precondition = precondition;

	if (do_lock) g_system->m_sync.enter();
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

	if (do_lock) g_system->m_sync.exit();
}


void enableBackupWorker(bool enable)
{
	MT::CriticalSectionLock lock(g_system->m_sync);

	for (WorkerTask* task : g_system->m_backup_workers) {
		if (task->m_is_enabled != enable) {
			task->m_is_enabled = enable;
			if (enable) {
				task->m_enabled.trigger();
			}
			else {
				task->m_enabled.reset();
			}
			return;
		}
	}

	ASSERT(enable);
	WorkerTask* task = LUMIX_NEW(g_system->m_allocator, WorkerTask)(*g_system, 0xff);
	if (task->create("Backup worker", false)) {
		g_system->m_backup_workers.push(task);
		task->m_is_enabled = true;
		task->m_is_backup = true;
		task->m_enabled.trigger();
	}
	else {
		logError("Engine") << "Job system backup worker failed to initialize.";
		LUMIX_DELETE(g_system->m_allocator, task);
	}
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
	static void __stdcall manage(void* data)
#else
	static void manage(void* data)
#endif
{
	g_system->m_sync.exit();

	FiberDecl* this_fiber = (FiberDecl*)data;
	Profiler::beginBlock("job management");
	Profiler::blockColor(0, 0, 0xff);
		
	WorkerTask* worker = getWorker();
	while (!worker->m_finished) {
		if (worker->m_is_backup) {
			if (!worker->m_enabled.poll()) {
				PROFILE_BLOCK("disabled");
				Profiler::blockColor(0xff, 0, 0xff);
				worker->m_enabled.wait();
			}
		}

		FiberDecl* fiber = nullptr;
		Job job;
		{
			MT::CriticalSectionLock lock(g_system->m_job_queue_sync);

			if (!worker->m_ready_fibers.empty()) {
				fiber = worker->m_ready_fibers.back();
				worker->m_ready_fibers.pop();
				if (worker->m_ready_fibers.empty()) worker->m_work_signal.reset();
			}
			else if (!worker->m_job_queue.empty()) {
				job = worker->m_job_queue.back();
				worker->m_job_queue.pop();
				if (worker->m_job_queue.empty()) worker->m_work_signal.reset();
			}
			else if (!g_system->m_ready_fibers.empty()) {
				fiber = g_system->m_ready_fibers.back();
				g_system->m_ready_fibers.pop();
				if (g_system->m_ready_fibers.empty()) g_system->m_work_signal.reset();
			}
			else if(!g_system->m_job_queue.empty()) {
				job = g_system->m_job_queue.back();
				g_system->m_job_queue.pop();
				if (g_system->m_job_queue.empty()) g_system->m_work_signal.reset();
			}
		}

		if (fiber) {
			Profiler::endBlock();
			worker->m_current_fiber = fiber;

			g_system->m_sync.enter();
            LUMIX_FATAL(!this_fiber->current_job.task);
			g_system->m_free_fibers.push(this_fiber);
			Fiber::switchTo(&getWorker()->m_current_fiber->fiber, fiber->fiber);
			g_system->m_sync.exit();

			Profiler::beginBlock("job management");
			Profiler::blockColor(0, 0, 0xff);
			worker = getWorker();
			worker->m_current_fiber = this_fiber;
			continue;
		}

		if (job.task) {
			Profiler::endBlock();
			Profiler::beginBlock("job");
			if (isValid(job.dec_on_finish) || isValid(job.precondition)) {
				Profiler::pushJobInfo(job.dec_on_finish, job.precondition);
			}
			this_fiber->current_job = job;
			job.task(job.data);
            this_fiber->current_job.task = nullptr;
			if (isValid(job.dec_on_finish)) {
				trigger(job.dec_on_finish);
			}
			worker = getWorker();
			Profiler::endBlock();
			Profiler::beginBlock("job management");
			Profiler::blockColor(0, 0, 0xff);
		}
		else 
		{
			PROFILE_BLOCK("idle");
			Profiler::blockColor(0xff, 0, 0xff);
			MT::Event::waitMultiple(g_system->m_work_signal, worker->m_work_signal, 1);
		}
	}
	Profiler::endBlock();
	Fiber::switchTo(&getWorker()->m_current_fiber->fiber, getWorker()->m_primary_fiber);
}


bool init(u8 workers_count, IAllocator& allocator)
{
	ASSERT(!g_system);

	g_system = LUMIX_NEW(allocator, System)(allocator);
	g_system->m_work_signal.reset();

	g_system->m_free_fibers.reserve(lengthOf(g_system->m_fiber_pool));
	for (FiberDecl& fiber : g_system->m_fiber_pool) {
		g_system->m_free_fibers.push(&fiber);
	}

	const int fiber_num = lengthOf(g_system->m_fiber_pool);
	for(int i = 0; i < fiber_num; ++i) { 
		FiberDecl& decl = g_system->m_fiber_pool[i];
		decl.idx = i;
	}

	int count = maximum(1, int(workers_count));
	for (int i = 0; i < count; ++i) {
		WorkerTask* task = LUMIX_NEW(allocator, WorkerTask)(*g_system, i < 64 ? u64(1) << i : 0);
		if (task->create("Worker", false)) {
			task->m_is_enabled = true;
			task->m_enabled.trigger();
			g_system->m_workers.push(task);
			task->setAffinityMask((u64)1 << i);
		}
		else {
			logError("Engine") << "Job system worker failed to initialize.";
			LUMIX_DELETE(allocator, task);
		}
	}

	return !g_system->m_workers.empty();
}


u8 getWorkersCount()
{
	const int c = g_system->m_workers.size();
	ASSERT(c <= 0xff);
	return (u8)c;
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
	for (MT::Task* task : g_system->m_backup_workers)
	{
		WorkerTask* wt = (WorkerTask*)task;
		wt->m_finished = true;
		wt->m_enabled.trigger();
	}

	for (MT::Task* task : g_system->m_backup_workers)
	{
		while (!task->isFinished()) g_system->m_work_signal.trigger();
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (MT::Task* task : g_system->m_workers)
	{
		while (!task->isFinished()) g_system->m_work_signal.trigger();
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (FiberDecl& fiber : g_system->m_fiber_pool)
	{
		if(fiber.fiber != Fiber::INVALID_FIBER) {
			Fiber::destroy(fiber.fiber);
		}
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
	
	if (getWorker()) {
		Profiler::blockColor(0xff, 0, 0);
		FiberDecl* this_fiber = getWorker()->m_current_fiber;

		runInternal(this_fiber, [](void* data){
			MT::CriticalSectionLock lock(g_system->m_job_queue_sync);
			FiberDecl* fiber = (FiberDecl*)data;
			if (fiber->current_job.worker_index == ANY_WORKER) {
				g_system->m_ready_fibers.push(fiber);
				g_system->m_work_signal.trigger();
			}
			else {
				WorkerTask* worker = g_system->m_workers[fiber->current_job.worker_index % g_system->m_workers.size()];
				worker->m_ready_fibers.push(fiber);
				worker->m_work_signal.trigger();
			}
		}, handle, false, nullptr, 0);
		
		const Profiler::FiberSwitchData& switch_data = Profiler::beginFiberWait(handle);
		FiberDecl* new_fiber = g_system->m_free_fibers.back();
		g_system->m_free_fibers.pop();
		if (new_fiber->fiber == Fiber::INVALID_FIBER) {
			new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
		}
		getWorker()->m_current_fiber = new_fiber;
		Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
		getWorker()->m_current_fiber = this_fiber;
		g_system->m_sync.exit();
		Profiler::endFiberWait(handle, switch_data);
		
		#ifdef LUMIX_DEBUG
			g_system->m_sync.enter();
			ASSERT(isSignalZero(handle, false));
			g_system->m_sync.exit();
		#endif
	}
	else
	{
		// TODO maybe handle this externally since main thread is no more
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
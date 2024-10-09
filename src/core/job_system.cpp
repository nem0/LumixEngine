#include "core/tag_allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/fibers.h"
#include "core/allocator.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/string.h"
#include "core/sync.h"
#include "core/thread.h"
#include "job_system.h"

namespace Lumix::jobs {

struct Job {
	void (*task)(void*) = nullptr;
	void* data = nullptr;
	Signal* dec_on_finish;
	u8 worker_index;
};

struct WorkerTask;

struct FiberDecl {
	int idx;
	Fiber::Handle fiber = Fiber::INVALID_FIBER;
	Job current_job;
};

#ifdef _WIN32
	static void __stdcall manage(void* data);
#else
	static void manage(void* data);
#endif

struct Work {
	Work() : type(NONE) {}
	Work(const Job& job) : job(job), type(JOB) {}
	Work(FiberDecl* fiber) : fiber(fiber), type(FIBER) {}
	union {
		Job job;
		FiberDecl* fiber;
	};
	enum Type {
		FIBER,
		JOB,
		NONE
	};
	Type type;
};

// Thread-safe, mutex-guarded ring buffer, with a fallback array for overflow
// with added semaphore for waiting on new work
template <int CAPACITY>
struct WorkQueue {
	WorkQueue(IAllocator& allocator) 
		: semaphore(0, 0x7fffFFff)
		, fallback(allocator)
	{}

	LUMIX_FORCE_INLINE void push(const Work& obj) {
		Lumix::MutexGuard guard(mutex);
		if (write - read >= lengthOf(objects)) {
			// ring buffer full, push to fallback
			fallback.push(obj);
			semaphore.signal();
			return;
		}
		
		objects[write % lengthOf(objects)] = obj;
		++write;
		semaphore.signal();
	}
	
	LUMIX_FORCE_INLINE bool pop(Work& obj) {
		Lumix::MutexGuard guard(mutex);
		if (read == write) {
			// ring buffer empty, check fallback lifo
			if (fallback.empty()) return false;

			obj = fallback.back();
			fallback.pop();
			semaphore.wait(0);
			return true;
		}

		// pop from ring buffer
		obj = objects[read % lengthOf(objects)];
		++read;
		semaphore.wait(0);
		return true;
	}

	Semaphore semaphore;
	Array<Work> fallback;
	Work objects[CAPACITY];
	u32 read = 0;
	u32 write = 0;
	Lumix::Mutex mutex;
};

struct System {
	System(IAllocator& allocator) 
		: m_allocator(allocator, "job system")
		, m_workers(m_allocator)
		, m_free_fibers(m_allocator)
		, m_backup_workers(m_allocator)
		, m_work_queue(m_allocator)
	{}


	TagAllocator m_allocator;
	Lumix::Mutex m_sync;
	Array<WorkerTask*> m_workers;
	Array<WorkerTask*> m_backup_workers;
	FiberDecl m_fiber_pool[512];
	Array<FiberDecl*> m_free_fibers;
	WorkQueue<64> m_work_queue;
};


static Local<System> g_system;

static AtomicI32 g_generation = 0;
static thread_local WorkerTask* g_worker = nullptr;

#ifndef _WIN32
	#pragma clang optimize off 
#endif
#pragma optimize( "", off )
WorkerTask* getWorker()
{
	return g_worker;
}
#pragma optimize( "", on )
#ifndef _WIN32
	#pragma clang optimize on
#endif

struct WorkerTask : Thread {
	WorkerTask(System& system, u8 worker_index) 
		: Thread(system.m_allocator)
		, m_system(system)
		, m_worker_index(worker_index)
		, m_work_queue(system.m_allocator)
	{}

	i32 task() override {
		profiler::showInProfiler(true);
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
		if (!Fiber::isValid(fiber->fiber)) {
			fiber->fiber = Fiber::create(64 * 1024, manage, fiber);
		}
		getWorker()->m_current_fiber = fiber;
		Fiber::switchTo(&getWorker()->m_primary_fiber, fiber->fiber);
	}


	bool m_finished = false;
	FiberDecl* m_current_fiber = nullptr;
	Fiber::Handle m_primary_fiber;
	System& m_system;
	WorkQueue<4> m_work_queue;
	u8 m_worker_index;
	bool m_is_enabled = false;
	bool m_is_backup = false;
};

// linked list of  fibers waiting on a signal
struct Waitor {
	Waitor* next;
	FiberDecl* fiber;
};

// decrement signal counter
// if new value == 0, schedule all fibers waiting on a signal
// returns true if any fiber was scheduled
template <bool ZERO>
LUMIX_FORCE_INLINE static bool trigger(Signal* signal)
{
	Waitor* waitor = nullptr;
	{
		Lumix::MutexGuard lock(g_system->m_sync);

		// decrement signal counter
		if constexpr (ZERO) {
			signal->counter = 0;
		}
		else {
			i32 counter = signal->counter.dec();
			ASSERT(counter > 0);
			if (counter > 1) return false;
		}

		// store waitor in local variable, since it can change once we unlock the mutex
		waitor = signal->waitor;
		signal->waitor = nullptr;
	}
	if (!waitor) return false;

	{
		// schedule all fibers waiting on the signal
		while (waitor) {
			// store `next` in local variable, since it can be invalidated after we push the fiber to the queue
			Waitor* next = waitor->next;
			const u8 worker_idx = waitor->fiber->current_job.worker_index;
			// push to queue
			if (worker_idx == ANY_WORKER) {
				g_system->m_work_queue.push(waitor->fiber);
			}
			else {
				WorkerTask* worker = g_system->m_workers[worker_idx % g_system->m_workers.size()];
				worker->m_work_queue.push(waitor->fiber);
			}
			waitor = next;
		}
	}

	return true;
}

void enableBackupWorker(bool enable)
{
	Lumix::MutexGuard lock(g_system->m_sync);

	for (WorkerTask* task : g_system->m_backup_workers) {
		if (task->m_is_enabled != enable) {
			task->m_is_enabled = enable;
			return;
		}
	}

	ASSERT(enable);
	WorkerTask* task = LUMIX_NEW(g_system->m_allocator, WorkerTask)(*g_system, 0xff);
	if (task->create("Backup worker", false)) {
		g_system->m_backup_workers.push(task);
		task->m_is_enabled = true;
		task->m_is_backup = true;
	}
	else {
		logError("Job system backup worker failed to initialize.");
		LUMIX_DELETE(g_system->m_allocator, task);
	}
}

LUMIX_FORCE_INLINE static bool setRedEx(Signal* signal) {
	ASSERT(signal);
	ASSERT(signal->counter <= 1);
	bool res = signal->counter.compareExchange(1, 0);
	if (res) {
		signal->generation = g_generation.inc();
	}
	return res;
}

void setRed(Signal* signal) {
	setRedEx(signal);
}

void setGreen(Signal* signal) {
	ASSERT(signal);
	ASSERT(signal->counter <= 1);
	const u32 gen = signal->generation;
	if (trigger<true>(signal)) {
		profiler::signalTriggered(gen);
	}
}


void run(void* data, void(*task)(void*), Signal* on_finished, u8 worker_index)
{
	Job job;
	job.data = data;
	job.task = task;
	job.worker_index = worker_index != ANY_WORKER ? worker_index % getWorkersCount() : worker_index;
	job.dec_on_finish = on_finished;

	if (on_finished) {
		Lumix::MutexGuard guard(g_system->m_sync);
		if (on_finished->counter.inc() == 0) {
			on_finished->generation = g_generation.inc();
		}
	}

	if (worker_index != ANY_WORKER) {
		WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
		worker->m_work_queue.push(job);
		return;
	}

	g_system->m_work_queue.push(job);
}

// try to pop a job from the queues (first worker, then global), if no job is available, go to sleep
static bool tryPopWork(Work& work, WorkerTask* worker) {
	if (worker->m_work_queue.pop(work)) return true;
	if (g_system->m_work_queue.pop(work)) return true;

	{
		// no jobs, let's go to sleep
		PROFILE_BLOCK("sleeping");
		profiler::blockColor(0x30, 0x30, 0x30);
		
		const i32 signaled_obj = Semaphore::waitMultiple(worker->m_work_queue.semaphore, g_system->m_work_queue.semaphore);
		// somebody push a job to the queue, so we woke up
		if (signaled_obj == 0) {
			return worker->m_work_queue.pop(work);
		}
		else if (signaled_obj == 1) {
			return g_system->m_work_queue.pop(work);
		}
		else {
			ASSERT(false);
		}
	}

	return false;
}

#ifdef _WIN32
	static void __stdcall manage(void* data)
#else
	static void manage(void* data)
#endif
{
	g_system->m_sync.exit();

	FiberDecl* this_fiber = (FiberDecl*)data;
		
	WorkerTask* worker = getWorker();
	while (!worker->m_finished) {
		if (worker->m_is_backup) {
			Lumix::MutexGuard guard(g_system->m_sync);
			while (!worker->m_is_enabled && !worker->m_finished) {
				PROFILE_BLOCK("disabled");
				profiler::blockColor(0xff, 0, 0xff);
				worker->sleep(g_system->m_sync);
			}
		}

		Work work;
		while (!worker->m_finished) {
			if (tryPopWork(work, worker)) break;
			if (worker->m_is_backup) break;
		}
		if (worker->m_finished) break;

		if (work.type == Work::FIBER) {
			worker->m_current_fiber = work.fiber;

			g_system->m_sync.enter();
			g_system->m_free_fibers.push(this_fiber);
			Fiber::switchTo(&this_fiber->fiber, work.fiber->fiber);
			g_system->m_sync.exit();

			worker = getWorker();
			worker->m_current_fiber = this_fiber;
		}
		else if (work.type == Work::JOB) {
			if (!work.job.task) continue;

			profiler::beginBlock("job");
			profiler::blockColor(0x60, 0x60, 0x60);
			if (work.job.dec_on_finish) {
				profiler::pushJobInfo(work.job.dec_on_finish->generation);
			}
			this_fiber->current_job = work.job;
			work.job.task(work.job.data);
			this_fiber->current_job.task = nullptr;
			if (work.job.dec_on_finish) {
				trigger<false>(work.job.dec_on_finish);
			}
			worker = getWorker();
			profiler::endBlock();
		}
		else ASSERT(worker->m_is_backup);
	}
	Fiber::switchTo(&this_fiber->fiber, getWorker()->m_primary_fiber);
}

IAllocator& getAllocator() {
	return g_system->m_allocator;
}

bool init(u8 workers_count, IAllocator& allocator)
{
	g_system.create(allocator);

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
		WorkerTask* task = LUMIX_NEW(getAllocator(), WorkerTask)(*g_system, i);
		if (task->create(StaticString<64>("Worker #", i), false)) {
			task->m_is_enabled = true;
			g_system->m_workers.push(task);
			task->setAffinityMask((u64)1 << i);
		}
		else {
			logError("Job system worker failed to initialize.");
			LUMIX_DELETE(getAllocator(), task);
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
	IAllocator& allocator = g_system->m_allocator;
	for (Thread* task : g_system->m_workers)
	{
		WorkerTask* wt = (WorkerTask*)task;
		wt->m_finished = true;
	}
	for (Thread* task : g_system->m_backup_workers) {
		WorkerTask* wt = (WorkerTask*)task;
		wt->m_finished = true;
		wt->wakeup();
	}

	for (Thread* task : g_system->m_backup_workers)
	{
		while (!task->isFinished()) task->wakeup();
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (WorkerTask* task : g_system->m_workers)
	{
		while (!task->isFinished()) task->wakeup();
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (FiberDecl& fiber : g_system->m_fiber_pool)
	{
		if(Fiber::isValid(fiber.fiber)) {
			Fiber::destroy(fiber.fiber);
		}
	}

	g_system.destroy();
}

static void waitEx(Signal* signal, bool is_mutex)
{
	ASSERT(signal);
	g_system->m_sync.enter();

	if (signal->counter == 0) {
		g_system->m_sync.exit();
		return;
	}
	
	if (!getWorker()) {
		while (signal->counter > 0) {
			g_system->m_sync.exit();
			os::sleep(1);
			g_system->m_sync.enter();
		}
		g_system->m_sync.exit();
		return;
	}

	FiberDecl* this_fiber = getWorker()->m_current_fiber;

	Waitor waitor;
	waitor.fiber = this_fiber;
	waitor.next = signal->waitor;
	signal->waitor = &waitor;

	const profiler::FiberSwitchData& switch_data = profiler::beginFiberWait(signal->generation, is_mutex);
	FiberDecl* new_fiber = g_system->m_free_fibers.back();
	g_system->m_free_fibers.pop();
	if (!Fiber::isValid(new_fiber->fiber)) {
		new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
	}
	getWorker()->m_current_fiber = new_fiber;
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	getWorker()->m_current_fiber = this_fiber;
	g_system->m_sync.exit();
	profiler::endFiberWait(switch_data);
}

void enter(Mutex* mutex) {
	ASSERT(getWorker());
	for (;;) {
		for (u32 i = 0; i < 400; ++i) {
			if (setRedEx(&mutex->signal)) return;
		}
		waitEx(&mutex->signal, true);
	}
}

void exit(Mutex* mutex) {
	ASSERT(getWorker());
	setGreen(&mutex->signal);
}

void wait(Signal* handle) {
	waitEx(handle, false);
}

void moveJobToWorker(u8 worker_index) {
	g_system->m_sync.enter();
	FiberDecl* this_fiber = getWorker()->m_current_fiber;
	WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
	worker->m_work_queue.push(this_fiber);
	FiberDecl* new_fiber = g_system->m_free_fibers.back();
	g_system->m_free_fibers.pop();
	if (!Fiber::isValid(new_fiber->fiber)) {
		new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
	}
	getWorker()->m_current_fiber = new_fiber;
	this_fiber->current_job.worker_index = worker_index;
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	getWorker()->m_current_fiber = this_fiber;
	ASSERT(getWorker()->m_worker_index == worker_index);
	g_system->m_sync.exit();
}

void yield() {
	g_system->m_sync.enter();
	FiberDecl* this_fiber = getWorker()->m_current_fiber;
	g_system->m_work_queue.push(this_fiber);

	FiberDecl* new_fiber = g_system->m_free_fibers.back();
	g_system->m_free_fibers.pop();
	if (!Fiber::isValid(new_fiber->fiber)) {
		new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
	}
	this_fiber->current_job.worker_index = ANY_WORKER;
	getWorker()->m_current_fiber = new_fiber;
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	g_system->m_sync.exit();
}


} // namespace Lumix::jobs

#include "core/allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/fibers.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/ring_buffer.h"
#include "core/string.h"
#include "core/sync.h"
#include "core/tag_allocator.h"
#include "core/thread.h"
#include "job_system.h"

namespace Lumix::jobs {

struct Job {
	void (*task)(void*) = nullptr;
	void* data = nullptr;
	Counter* dec_on_finish;
	u8 worker_index;
};

struct WorkerTask;

struct FiberJobPair {
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
	Work(FiberJobPair* fiber) : fiber(fiber), type(FIBER) {}
	union {
		Job job;
		FiberJobPair* fiber;
	};
	enum Type {
		FIBER,
		JOB,
		NONE
	};
	Type type;
};

// Thread-safe, lockless ring buffer, with a mutex-guarded fallback array for overflow
// with added semaphore for waiting on new work
template <int CAPACITY>
struct WorkQueue : RingBuffer<Work, CAPACITY> {
	WorkQueue(IAllocator& allocator) 
		: semaphore(0, 0x7fffFFff)
		, RingBuffer<Work, CAPACITY>(allocator)
	{}

	LUMIX_FORCE_INLINE void push(const Work& obj) {
		RingBuffer<Work, CAPACITY>::push(obj);
		semaphore.signal();
	}

	// optimized, batch push multiple jobs
	LUMIX_FORCE_INLINE void pushN(const Work& obj, u32 num_jobs) {
		for (u32 i = 0; i < num_jobs; ++i) {
			RingBuffer<Work, CAPACITY>::push(obj);
		}
		semaphore.signal(num_jobs);
	}
	
	LUMIX_FORCE_INLINE bool pop(Work& obj) {
		if (!RingBuffer<Work, CAPACITY>::pop(obj)) return false;
		semaphore.wait(0);
		return true;
	}

	Semaphore semaphore;
};

struct System {
	System(IAllocator& allocator) 
		: m_allocator(allocator, "job system")
		, m_workers(m_allocator)
		, m_free_fibers(m_allocator)
		, m_work_queue(m_allocator)
	{}

	TagAllocator m_allocator;
	Array<WorkerTask*> m_workers;
	FiberJobPair m_fiber_pool[512];
	RingBuffer<FiberJobPair*, 512> m_free_fibers;
	WorkQueue<256> m_work_queue;
};


static Local<System> g_system;

static AtomicI32 g_generation = 0;
static thread_local WorkerTask* g_worker = nullptr;

#ifndef _WIN32
	#pragma clang optimize off 
#endif
#pragma optimize( "", off )
// optimizer can mess up g_worker value across fiber switches, but it seems to work fine with when using getWorker()
WorkerTask* getWorker()
{
	return g_worker;
}
#pragma optimize( "", on )
#ifndef _WIN32
	#pragma clang optimize on
#endif

LUMIX_FORCE_INLINE static FiberJobPair* popFreeFiber() {
	FiberJobPair* new_fiber;
	bool popped = g_system->m_free_fibers.pop(new_fiber);
	ASSERT(popped);
	if (!Fiber::isValid(new_fiber->fiber)) {
		new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
	}
	return new_fiber;
}

// intrusive linked list of fibers waiting on a signal/mutex
struct WaitingFiber {
	WaitingFiber* next;
	FiberJobPair* fiber;
};

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
		FiberJobPair* fiber = popFreeFiber();
		WorkerTask* worker = getWorker();
		worker->m_current_fiber = fiber;
		Fiber::switchTo(&worker->m_primary_fiber, fiber->fiber);
	}

	bool m_finished = false;
	FiberJobPair* m_current_fiber = nullptr;

	Signal* m_signal_to_check = nullptr;
	WaitingFiber* m_waiting_fiber_to_push = nullptr;
	
	Fiber::Handle m_primary_fiber;
	System& m_system;
	WorkQueue<4> m_work_queue;
	u8 m_worker_index;
	bool m_is_enabled = false;
};

void addCounter(Counter* counter, u32 value) {
	if (counter->value.add(value) == 0) {
		counter->signal.generation = g_generation.inc();
		turnRed(&counter->signal);
	};
}


// push fiber to work queue
LUMIX_FORCE_INLINE static void scheduleFiber(FiberJobPair* fiber) {
	const u8 worker_idx = fiber->current_job.worker_index;
	if (worker_idx == ANY_WORKER) {
		g_system->m_work_queue.push(fiber);
	} else {
		WorkerTask* worker = g_system->m_workers[worker_idx % g_system->m_workers.size()];
		worker->m_work_queue.push(fiber);
	}
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

// check if the fiber before us wanted to be parked on a signal
// fibers can not park themselves locklessly, because 
// they could get unparked before actually switching fibers
// so they need other fibers to actually park them
static void afterSwitch() {
	WorkerTask* worker = getWorker();
	
	if (!worker->m_signal_to_check) return;

	Signal* signal = worker->m_signal_to_check;
	WaitingFiber* fiber = worker->m_waiting_fiber_to_push;
	worker->m_signal_to_check = nullptr;

	for (;;) {
		const i64 counter = signal->state;
		
		// signal is green, repush the fiber
		if ((counter & 1) == 0) {
			scheduleFiber(fiber->fiber);
			return;
		}

		// signal is red, let's try to actually park the fiber
		fiber->next = (WaitingFiber*)u64(counter & ~i64(1));
		const i64 new_counter = 1 | i64(fiber);
		// try to update the signal state, if nobody changed it in the meantime
		if (signal->state.compareExchange(new_counter, counter)) return;
		
		// somebody changed the signal state, let's try again
	}
}

// switch from current fiber to a new, free fiber (into `manage` function)
LUMIX_FORCE_INLINE static void switchFibers(i32 profiler_id) {
	WorkerTask* worker = getWorker();
	FiberJobPair* this_fiber = worker->m_current_fiber;
	
	const profiler::FiberSwitchData switch_data = profiler::beginFiberWait(profiler_id);
	FiberJobPair* new_fiber = popFreeFiber();
	worker->m_current_fiber = new_fiber;
	
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	afterSwitch();
	
	// we can be on different worker than before fiber switch, must call getWorker()
	getWorker()->m_current_fiber = this_fiber;
	profiler::endFiberWait(switch_data);
}

void turnGreenEx(Signal* signal) {
	ASSERT(getWorker());

	// turn the signal green
	const i64 old_state = signal->state.exchange(0);
	
	// wake up all waiting fibers
	WaitingFiber* fiber = (WaitingFiber*)(old_state & ~i64(1));
	while (fiber) {
		WaitingFiber* next = fiber->next;
		scheduleFiber(fiber->fiber);
		fiber = next;
	}
}

void turnGreen(Signal* signal) {
	turnGreenEx(signal);
	profiler::signalTriggered(signal->generation);
}

#ifdef _WIN32
	static void __stdcall manage(void* data)
#else
	static void manage(void* data)
#endif
{
	afterSwitch();

	FiberJobPair* this_fiber = (FiberJobPair*)data;
		
	WorkerTask* worker = getWorker();
	while (!worker->m_finished) {
		Work work;
		while (!worker->m_finished && !tryPopWork(work, worker)) {}
		if (worker->m_finished) break;

		if (work.type == Work::FIBER) {
			worker->m_current_fiber = work.fiber;

			g_system->m_free_fibers.push(this_fiber);
			Fiber::switchTo(&this_fiber->fiber, work.fiber->fiber);
			afterSwitch();

			worker = getWorker();
			worker->m_current_fiber = this_fiber;
		}
		else if (work.type == Work::JOB) {
			if (!work.job.task) continue;

			profiler::beginBlock("job");
			profiler::blockColor(0x60, 0x60, 0x60);
			if (work.job.dec_on_finish) {
				profiler::pushJobInfo(work.job.dec_on_finish->signal.generation);
			}
			this_fiber->current_job = work.job;
			work.job.task(work.job.data);
			this_fiber->current_job.task = nullptr;
			if (work.job.dec_on_finish) {
				if (work.job.dec_on_finish->value.dec() == 1) {
					turnGreenEx(&work.job.dec_on_finish->signal);
				}
			}
			worker = getWorker();
			profiler::endBlock();
		}
		else ASSERT(false);
	}
	Fiber::switchTo(&this_fiber->fiber, getWorker()->m_primary_fiber);
}

IAllocator& getAllocator() {
	return g_system->m_allocator;
}

bool init(u8 workers_count, IAllocator& allocator) {
	g_system.create(allocator);

	for (FiberJobPair& fiber : g_system->m_fiber_pool) {
		g_system->m_free_fibers.push(&fiber);
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

	for (WorkerTask* task : g_system->m_workers)
	{
		while (!task->isFinished()) {
			g_system->m_work_queue.semaphore.signal();
		}
		task->destroy();
		LUMIX_DELETE(allocator, task);
	}

	for (FiberJobPair& fiber : g_system->m_fiber_pool)
	{
		if(Fiber::isValid(fiber.fiber)) {
			Fiber::destroy(fiber.fiber);
		}
	}

	g_system.destroy();
}

void turnRed(Signal* signal) {
	if ((signal->state.setBits(1) & 1) == 0) {
		signal->generation = g_generation.inc();
	}
}

void wait(Counter* counter) {
	wait(&counter->signal);
}

void wait(Signal* signal) {
	ASSERT(getWorker());

	// spin for a bit
	for (u32 i = 0; i < 40; ++i) {
		// fast path
		if ((signal->state & 1) == 0) return;
	}

	// we busy-waited too long, let's park the fiber
	// the signal state is rechecked after we switch to other fiber
	// we can't park the fiber here, because it could get unparked before we switch to another fiber
	WorkerTask* worker = getWorker();

	WaitingFiber waiting_fiber;
	waiting_fiber.fiber = worker->m_current_fiber;
	worker->m_signal_to_check = signal;
	worker->m_waiting_fiber_to_push = &waiting_fiber;
	
	switchFibers(signal->generation);
}

void waitAndTurnRed(Signal* signal) {
	ASSERT(getWorker());
	for (;;) {
		// fastest path
		if (signal->state.bitTestAndSet(0)) {
			signal->generation = g_generation.inc();
			ASSERT(signal->state & 1);
			return;
		}

		// spin for a bit
		for (u32 i = 0; i < 40; ++i) {
			// only read
			if (signal->state == 0) break;
			cpuRelax(); // rep nop, hint to cpu core to conserve resources (switch to lower power state)
		}
		
		// check once again before parking the fiber
		if (signal->state.bitTestAndSet(0)) {
			signal->generation = g_generation.inc();
			ASSERT(signal->state & 1);
			return;
		}

		// we busy-waited too long, let's park the fiber
		// the mutex state is rechecked after we switch to other fiber
		// we can't park the fiber here, because it could get unparked before we switch to another fiber
		WorkerTask* worker = getWorker();

		WaitingFiber waiting_fiber;
		waiting_fiber.fiber = worker->m_current_fiber;
		worker->m_signal_to_check = signal;
		worker->m_waiting_fiber_to_push = &waiting_fiber;
		
		switchFibers(signal->generation);
	}
}

void enter(Mutex* mutex) {
	waitAndTurnRed(&mutex->signal);
}

void exit(Mutex* mutex) {
	// this is almost the same as turnGreen, but it **pops only** one of the waiting fibers, since only one can enter the mutex
	ASSERT(getWorker());
	ASSERT(mutex->signal.state & 1);

	for (;;) {
		const i64 state = mutex->signal.state;

		// prepare to pop one waiting fiber from the mutex and unlock the mutex
		WaitingFiber* waiting_fiber = (WaitingFiber*)(state & ~i64(1));
		const i64 new_state_value = i64(waiting_fiber ? waiting_fiber->next : nullptr);

		// if something changed (there's a new fiber waiting), try again
		if (!mutex->signal.state.compareExchange(new_state_value, state)) continue;

		if (waiting_fiber) {
			// we managed to pop one waiting fiber from the mutex
			// push the popped fiber to the work queue
			// so it can try to enter the mutex
			scheduleFiber(waiting_fiber->fiber);
		}
		return;
	}
}

void moveJobToWorker(u8 worker_index) {
	FiberJobPair* this_fiber = getWorker()->m_current_fiber;
	WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
	worker->m_work_queue.push(this_fiber);
	FiberJobPair* new_fiber = popFreeFiber();
	getWorker()->m_current_fiber = new_fiber;
	this_fiber->current_job.worker_index = worker_index;
	
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	afterSwitch();
	getWorker()->m_current_fiber = this_fiber;
	ASSERT(getWorker()->m_worker_index == worker_index);
}

void yield() {
	FiberJobPair* this_fiber = getWorker()->m_current_fiber;
	g_system->m_work_queue.push(this_fiber);

	FiberJobPair* new_fiber = popFreeFiber();
	this_fiber->current_job.worker_index = ANY_WORKER;
	getWorker()->m_current_fiber = new_fiber;
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	afterSwitch();
	getWorker()->m_current_fiber = this_fiber;
}

void run(void* data, void(*task)(void*), Counter* on_finished, u8 worker_index)
{
	Job job;
	job.data = data;
	job.task = task;
	job.worker_index = worker_index != ANY_WORKER ? worker_index % getWorkersCount() : worker_index;
	job.dec_on_finish = on_finished;

	if (on_finished) {
		addCounter(on_finished, 1);
	}

	if (worker_index != ANY_WORKER) {
		WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
		worker->m_work_queue.push(job);
		return;
	}

	g_system->m_work_queue.push(job);
}

void runN(void* data, void(*task)(void*), Counter* on_finished, u8 worker_index, u32 num_jobs)
{
	Job job;
	job.data = data;
	job.task = task;
	job.worker_index = worker_index != ANY_WORKER ? worker_index % getWorkersCount() : worker_index;
	job.dec_on_finish = on_finished;

	if (on_finished) {
		addCounter(on_finished, num_jobs);
	}

	if (worker_index != ANY_WORKER) {
		WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
		worker->m_work_queue.pushN(job, num_jobs);
		return;
	}

	g_system->m_work_queue.pushN(job, num_jobs);
}

} // namespace Lumix::jobs

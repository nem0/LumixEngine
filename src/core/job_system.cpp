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

/* There are three types of queues:
* 1. Work Stealing Queue - Each worker has its own. Jobs can only be pushed by the worker itself but can be consumed by any worker.
* 2. Worker Queue - Each worker has its own queue for jobs pinned to that worker.
		Jobs in this queue are executed only by the owning worker.
		Any thread, including those outside the job system, can push jobs to this queue.
* 3. Global Queue - A single global queue where jobs can be executed by any worker (unlike queue 2.).
		Any thread, including those outside the job system, can push jobs to this queue (unlike queue 1.).
*/

/* Invariants:
	* Jobs are executed in undefined order, i.e. if we push jobs A and B, we can't be sure that A will be executed before B. 
	* tryPop in sequence "push(), tryPop()" is guaranteed to pop a job. The consumer in this case can be on a different thread, if we are sure that push() returned.
	* If a thread calls push(jobA), tryPop() running in parallel on another thread might or might not pop jobA.
*/

#define LUMIX_PROFILE_JOBS

namespace Lumix::jobs {

struct Job {
	void (*task)(void*) = nullptr;
	void* data = nullptr;
	Counter* dec_on_finish;
	u8 worker_index;
};

struct WorkerTask;
static constexpr u64 STATE_COUNTER_MASK = 0xffFF;
static constexpr u64 STATE_WAITING_FIBER_MASK = (~u64(0)) & ~STATE_COUNTER_MASK;

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
	enum Type {
		FIBER,
		JOB,
		NONE
	};
	Type type;
	union {
		Job job;
		FiberJobPair* fiber;
	};
};

LUMIX_FORCE_INLINE static void wake(WorkerTask& to_wake);
LUMIX_FORCE_INLINE static void wake(u32 num_jobs);
LUMIX_FORCE_INLINE static void wake();
LUMIX_FORCE_INLINE static void executeJob(const Job& job);

// single producer, multiple consumer queue
// producer can call only push and tryPop - produce and consume on one end of the queue
// others can call only trySteal - consume from the other end of the queue
struct WorkStealingQueue {
	static constexpr u32 QUEUE_SIZE = 512;
	// Once we reach OVERFLOW_GUARD items in queue, we start executing jobs directly
	// instead of pushing them in queue. This is to prevent queue overflow.
	// Queue should be big enough for this to never happen.
	// Be aware that an overflow can still occur if a fiber is pushed to an already full queue. // TODO handle fiber overflow
	static constexpr u32 OVERFLOW_GUARD = QUEUE_SIZE - 4;
	static constexpr u32 SIZE_MASK = QUEUE_SIZE - 1;

	// optimized batch push
	LUMIX_FORCE_INLINE void pushAndWakeN(const Work& obj, u32 num) {
		const i32 producing_end = m_producing_end;
		const i32 size = producing_end - m_stealing_end;

		if (size + num > OVERFLOW_GUARD && obj.type == Work::JOB) {
			for (u32 i = 0; i < num; ++i) {
				executeJob(obj.job);
			}
			return;
		}
		
		ASSERT(size + num <= QUEUE_SIZE);
		for (u32 i = 0; i < num; ++i) {
			m_queue[(producing_end + i) & SIZE_MASK] = obj;
		}
		m_producing_end = producing_end + num;
		wake(num);
	}

	LUMIX_FORCE_INLINE void pushAndWake(const Work& obj) {
		// there's only one producer so we don't need to worry about concurrent push or tryPop
		// and stealers do not modify m_producing_end
		// worst case scenario is that a concurrent stealer will not be able to steal the element we are pushing right now
		const i32 producing_end = m_producing_end;
		const i32 size = producing_end - m_stealing_end;
		ASSERT(size < QUEUE_SIZE);

		if (size > OVERFLOW_GUARD && obj.type == Work::JOB) {
			// queue is near full, execute the job directly so we won't overflow
			// queue should be big enough for this to never happen
			executeJob(obj.job);
			return;
		}

		m_queue[producing_end & SIZE_MASK] = obj;
		m_producing_end = producing_end + 1;
		wake();
	}

	bool tryPop(Work& obj) {
		for (;;) {
			const i32 producing_end = m_producing_end - 1;
			m_producing_end = producing_end;
			// decrement producing_end first so that concurrent stealers can't pop the same element without us knowing
			const i32 stealing_end = m_stealing_end;
			
			// queue is empty
			if (stealing_end > producing_end) {
				// reset to normal empty state (m_producing_end == m_stealing_end)
				m_producing_end = stealing_end;
				return false;
			}

			obj = m_queue[producing_end & SIZE_MASK];
			
			const bool is_last_element = stealing_end == producing_end;
			if (!is_last_element) {
				// we are not trying to pop the last element
				// and we decremented producing_end before,
				// so concurrent stealers can't pop the same element
				return true;
			}

			// we are trying to pop the last element
			// we need to handle concurrent stealers
			// we try to change m_stealing_end because that's the only thing that can be changed by stealers
			if (m_stealing_end.compareExchange(stealing_end + 1, stealing_end)) {
				// we were faster, reset to normal empty state (m_producing_end == m_stealing_end)
				m_producing_end = stealing_end + 1;
				return true;
			}

			// concurrent stealer was faster, queue is empty
			m_producing_end = stealing_end + 1;
			return false;
		}
	}

	bool trySteal(Work& obj) {
		for (;;) {
			const i32 stealing_end = m_stealing_end;
			const i32 producing_end = m_producing_end;
			
			const bool is_empty = stealing_end >= producing_end;
			if (is_empty) return false;

			obj = m_queue[stealing_end & SIZE_MASK];
			
			// sync with other concurrent stealers, or tryPop in case of the last remaining element
			if (m_stealing_end.compareExchange(stealing_end + 1, stealing_end)) {
				// we managed to pop the element
				return true;
			}

			// concurrent stealer or tryPop was faster, retry
		}
	}

	// TODO this does not have to be full atomic, we could use barriers instead
	// align, so they are not on the same cacheline, since they have different access patterns
	alignas(64) AtomicI32 m_stealing_end = 0; 	// both producer and consumers can write this
	alignas(64) AtomicI32 m_producing_end = 0; 	// only producer modifies this, consumers can read it
	// if m_producing_end > m_stealing_end, queue is not empty
	Work m_queue[QUEUE_SIZE];
};

// MPMC queue
// very fast tryPop on empty queue, otherwise using mutex 
struct WorkQueue {
	// queue can be modified only when holding mutex
	AtomicI32 empty = 1; // tryPop can just read this and not lock the mutex if the queue is empty
	Lumix::Mutex mutex;
	Array<Work> queue;

	WorkQueue(IAllocator& allocator) : queue(allocator) {}

	LUMIX_FORCE_INLINE bool tryPop(Work& obj) {
		// fastest path - empty queue is just one atomic read
		if (empty) return false;

		Lumix::MutexGuard guard(mutex);
		if (queue.empty()) {
			empty = 1;
			return false;
		}

		obj = queue.back();
		queue.pop();
		if (queue.empty()) empty = 1;
		return true;
	}

	LUMIX_FORCE_INLINE void pushAndWakeN(const Work& obj, u32 num) {
		{
			Lumix::MutexGuard guard(mutex);
			for (u32 i = 0; i < num; ++i) {
				queue.push(obj);
			}
			empty = 0;
		}
		wake(num);
	}

	LUMIX_FORCE_INLINE void pushAndWake(const Work& obj, WorkerTask* to_wake) {
		{
			Lumix::MutexGuard guard(mutex);
			queue.push(obj);
			empty = 0;
		}
		if (to_wake) wake(*to_wake);
		else wake();
	}
};

struct System {
	System(IAllocator& allocator) 
		: m_allocator(allocator, "job system")
		, m_workers(m_allocator)
		, m_free_fibers(m_allocator)
		, m_sleeping_workers(m_allocator)
		, m_global_queue(m_allocator)
	{}

	TagAllocator m_allocator;
	Array<WorkerTask*> m_workers;
	FiberJobPair m_fiber_pool[512];
	RingBuffer<FiberJobPair*, 512> m_free_fibers;
	WorkQueue m_global_queue; // non-worker threads must push here
	Lumix::Mutex m_sleeping_sync;
	AtomicI32 m_num_sleeping = 0; // if 0, we are sure that no worker is sleeping; if not 0, workers can be in any state
	Array<WorkerTask*> m_sleeping_workers;
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

LUMIX_FORCE_INLINE WaitingFiber* getWaitingFiberFromState(u64 state) {
	return (WaitingFiber*)((state & STATE_WAITING_FIBER_MASK) >> 16);
}

LUMIX_FORCE_INLINE u16 getCounterFromState(u64 state) {
	return u16(state & STATE_COUNTER_MASK);
}

LUMIX_FORCE_INLINE u64 makeStateValue(WaitingFiber* fiber, u16 counter) {
	return (u64(fiber) << 16) | counter;
}

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

	volatile bool m_finished = false;
	FiberJobPair* m_current_fiber = nullptr;

	Signal* m_signal_to_check = nullptr;
	WaitingFiber* m_waiting_fiber_to_push = nullptr;
	
	Fiber::Handle m_primary_fiber;
	System& m_system;
	WorkQueue m_work_queue; // for jobs that need to be pinned to a worker
	WorkStealingQueue m_wsq;
	u8 m_worker_index;
	
	// if m_is_sleeping == 0, we are sure that we are not sleeping
	// but if m_is_sleeping == 1, we are not sure if we are sleeping or not
	AtomicI32 m_is_sleeping = 0; 
};

// push fiber to work queue
LUMIX_FORCE_INLINE static void scheduleFiber(FiberJobPair* fiber) {
	const u8 worker_idx = fiber->current_job.worker_index;
	if (worker_idx == ANY_WORKER) {
		getWorker()->m_wsq.pushAndWake(fiber);
	} else {
		WorkerTask* worker = g_system->m_workers[worker_idx % g_system->m_workers.size()];
		worker->m_work_queue.pushAndWake(fiber, worker);
	}
}

// try to steal a job from any other worker
// we have to try all workers, otherwise we could miss a job
LUMIX_FORCE_INLINE static bool trySteal(Work& work) {
	Array<WorkerTask*>& workers = g_system->m_workers;
	const u32 num_workers = workers.size();	
	const u32 start = rand() % num_workers;
	for (u32 i = 0; i < num_workers; ++i) {
		const u32 idx = (start + i) % num_workers;
		if (workers[idx]->m_wsq.trySteal(work))
			return true;
	}
	return false;
}

// try to pop a job from the queues
LUMIX_FORCE_INLINE static bool tryPopWork(Work& work, WorkerTask* worker) {
	// jobs in worker's work queue are rare but usually in the critical path, so we need to try first
	// try on empty queue is very fast
	if (worker->m_work_queue.tryPop(work)) return true;
	
	// then try to pop a job from wsq first, since it's very fast
	if (worker->m_wsq.tryPop(work)) return true;
	
	// then try to steal a job from other workers, this is slower than tryPop
	if (trySteal(work)) return true;
	
	// it's very rare to have a job in the global queue, so we check it last
	if (g_system->m_global_queue.tryPop(work)) return true;

	// no jobs to pop
	return false;
}

// pops some work from the queues, if there are no jobs, worker goes to sleep
// returns true if there is some work to do
// return false if the worker should shutdown
LUMIX_FORCE_INLINE static bool popWork(Work& work, WorkerTask* worker) {
	while (!worker->m_finished) {
		if (tryPopWork(work, worker)) return true;

		// no jobs, let's mark the worker as going to sleep / sleeping
		g_system->m_num_sleeping.inc();
		worker->m_is_sleeping = 1;
		
		Lumix::MutexGuard guard(g_system->m_sleeping_sync);
		
		// we must recheck the queues while holding the mutex, because somebody might have pushed a job in the meantime
		if (tryPopWork(work, worker)) {
			g_system->m_num_sleeping.dec();
			worker->m_is_sleeping = 0;
			return true;
		}

		// no jobs, let's go to sleep
		// even if somebody pushed a job in the meantime, we are sure that we will be woken up, since we hold the mutex
		#ifdef LUMIX_PROFILE_JOBS
			PROFILE_BLOCK("sleeping");
			profiler::blockColor(0x30, 0x30, 0x30);
		#endif

		g_system->m_sleeping_workers.push(worker);
		worker->sleep(g_system->m_sleeping_sync);
		g_system->m_num_sleeping.dec();
		worker->m_is_sleeping = 0;
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
		const u64 state = (u64)signal->state;
		const u16 counter = getCounterFromState(state);
		
		// signal is green, repush the fiber
		if (counter == 0) {
			scheduleFiber(fiber->fiber);
			return;
		}

		// signal is red, let's try to actually park the fiber
		fiber->next = getWaitingFiberFromState(state);
		const u64 new_state = makeStateValue(fiber, counter);
		// try to update the signal state, if nobody changed it in the meantime
		if (signal->state.compareExchange(new_state, state)) return;
		
		// somebody changed the signal state, let's try again
	}
}

// switch from current fiber to a new, free fiber (into `manage` function)
LUMIX_FORCE_INLINE static void switchFibers(i32 profiler_id) {
	WorkerTask* worker = getWorker();
	FiberJobPair* this_fiber = worker->m_current_fiber;
	
	#ifdef LUMIX_PROFILE_JOBS
		const profiler::FiberSwitchData switch_data = profiler::beginFiberWait(profiler_id);
	#endif
	FiberJobPair* new_fiber = popFreeFiber();
	worker->m_current_fiber = new_fiber;
	
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	afterSwitch();
	
	// we can be on different worker than before fiber switch, must call getWorker()
	getWorker()->m_current_fiber = this_fiber;
	#ifdef LUMIX_PROFILE_JOBS
		profiler::endFiberWait(switch_data);
	#endif
}

void turnGreenEx(Signal* signal) {
	ASSERT(getWorker());

	// turn the signal green
	const u64 old_state = signal->state.exchange(0);
	
	// wake up all waiting fibers
	WaitingFiber* fiber = getWaitingFiberFromState(old_state);
	while (fiber) {
		WaitingFiber* next = fiber->next;
		scheduleFiber(fiber->fiber);
		fiber = next;
	}
}

void turnGreen(Signal* signal) {
	turnGreenEx(signal);
	#ifdef LUMIX_PROFILE_JOBS
		profiler::signalTriggered(signal->generation);
	#endif
}

LUMIX_FORCE_INLINE static void decCounter(Counter* counter) {
	for (;;) {
		const u64 state = counter->signal.state;
		WaitingFiber* fiber;
		u64 new_state;
		
		if (getCounterFromState(state) == 1) {
			// if we are going to turn the signal green
			fiber = getWaitingFiberFromState(state);
			new_state = 0;
		}
		else {
			// signal still red even after we decrement the counter
			fiber = nullptr;
			new_state = state - 1;
		}
		
		// decrement the counter if nobody changed the state in the meantime
		if (counter->signal.state.compareExchange(new_state, state)) {
			if (fiber) scheduleFiber(fiber->fiber);
			return;
		}
	}
}

LUMIX_FORCE_INLINE static void addCounter(Counter* counter, u32 value) {
	const u64 prev_state = counter->signal.state.add(value);
	ASSERT(getCounterFromState(prev_state) + value < 0xffFF);
	
	// if we turned the signal red
	if (getCounterFromState(prev_state) == 0) {
		counter->signal.generation = g_generation.inc();
	}
}

LUMIX_FORCE_INLINE static void executeJob(const Job& job) {
	#ifdef LUMIX_PROFILE_JOBS
		profiler::beginBlock("job");
		profiler::blockColor(0x60, 0x60, 0x60);
		if (job.dec_on_finish) {
			profiler::pushJobInfo(job.dec_on_finish->signal.generation);
		}
	#endif
	job.task(job.data);
	#ifdef LUMIX_PROFILE_JOBS
		profiler::endBlock();
	#endif
	if (job.dec_on_finish) {
		decCounter(job.dec_on_finish);
	}
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
		if (!popWork(work, worker)) break;

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

			this_fiber->current_job = work.job;

			executeJob(work.job);

			this_fiber->current_job.task = nullptr;
			worker = getWorker();
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
			task->wakeup();
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
		// it was green and we turned it red, so we need to increment generation
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
		if (getCounterFromState(signal->state) == 0) return;
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
		const u64 state = mutex->signal.state;

		// prepare to pop one waiting fiber from the mutex and unlock the mutex
		WaitingFiber* waiting_fiber = getWaitingFiberFromState(state);
		const u64 new_state_value = makeStateValue(waiting_fiber ? waiting_fiber->next : nullptr, 0);

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

// TODO race condition in both moveJobToWorker and yield, we could be poppped while we are still inside
void moveJobToWorker(u8 worker_index) {
	FiberJobPair* this_fiber = getWorker()->m_current_fiber;
	WorkerTask* worker = g_system->m_workers[worker_index % g_system->m_workers.size()];
	worker->m_work_queue.pushAndWake(this_fiber, worker);
	
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
	WorkerTask* worker = getWorker();
	g_system->m_global_queue.pushAndWake(this_fiber, nullptr);

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
		worker->m_work_queue.pushAndWake(job, worker);
		return;
	}

	WorkerTask* worker = getWorker();
	if (worker) {
		worker->m_wsq.pushAndWake(job);
		return;
	}

	g_system->m_global_queue.pushAndWake(job, nullptr);
}

void runN(void* data, void(*task)(void*), Counter* on_finished, u32 num_jobs)
{
	Job job;
	job.data = data;
	job.task = task;
	job.worker_index = ANY_WORKER;
	job.dec_on_finish = on_finished;

	if (on_finished) {
		addCounter(on_finished, num_jobs);
	}

	WorkerTask* worker = getWorker();
	if (worker) worker->m_wsq.pushAndWakeN(job, num_jobs);
	else g_system->m_global_queue.pushAndWakeN(job, num_jobs);
}

// wake the worker (if any is sleeping)
LUMIX_FORCE_INLINE static void wake(WorkerTask& worker) {
	if (!worker.m_is_sleeping) return;

	Lumix::MutexGuard guard(g_system->m_sleeping_sync);
	g_system->m_sleeping_workers.eraseItem(&worker);
	worker.wakeup();
}

// wake one worker (if any is sleeping)
LUMIX_FORCE_INLINE static void wake() {
	if (g_system->m_num_sleeping == 0) return;

	Lumix::MutexGuard guard(g_system->m_sleeping_sync);
	if (g_system->m_sleeping_workers.empty()) return;
	
	WorkerTask* to_wake = g_system->m_sleeping_workers.back();
	g_system->m_sleeping_workers.pop();
	to_wake->wakeup();
};


// wake num workers (or all if num > number of sleeping workers)
LUMIX_FORCE_INLINE static void wake(u32 num) {
	// fast path, no workers are sleeping
	if (g_system->m_num_sleeping == 0) return;

	Lumix::MutexGuard guard(g_system->m_sleeping_sync);
	for (u32 i = 0; i < num; ++i) {
		if (g_system->m_sleeping_workers.empty()) return;
		
		WorkerTask* to_wake = g_system->m_sleeping_workers.back();
		g_system->m_sleeping_workers.pop();
		to_wake->wakeup();
	}
}


} // namespace Lumix::jobs

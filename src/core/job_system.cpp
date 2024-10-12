#include "core/tag_allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/fibers.h"
#include "core/allocator.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/ring_buffer.h"
#include "core/string.h"
#include "core/sync.h"
#include "core/thread.h"
#include "job_system.h"

namespace Lumix::jobs {

static constexpr i32 is_red_bit = 1; // can be changed only if inner lock is not set
static constexpr i32 signal_has_parked_bit = 1 << 30; // can be changed only when m_sync is locked

static constexpr i32 is_locked_bit = 1; // can only be changed when has_parked_bit is not set
static constexpr i32 mutex_has_parked_bit = 2; // can only be changed when m_sync is locked

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
	Lumix::Mutex m_sync;
	Array<WorkerTask*> m_workers;
	FiberDecl m_fiber_pool[512];
	Array<FiberDecl*> m_free_fibers;
	WorkQueue<256> m_work_queue;
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
};

// linked list of fibers waiting on a signal
struct Waitor {
	Waitor* next;
	FiberDecl* fiber;
};

void setRed(BinarySignal* signal) {
	const bool changed = (signal->state.setBits(is_red_bit) & is_red_bit) == 0;
	if (changed) {
		signal->generation = g_generation.inc();
	}
}

void setGreen(BinarySignal* signal) {
	// fast path - no one is waiting and already green
	if (signal->state == 0) return;
	// fast path - red, but no one is waiting
	if (signal->state.compareExchange(0, is_red_bit)) return;
	
	// unlink waitors from signal
	Waitor* waitor = [&](){
		Lumix::MutexGuard guard(g_system->m_sync);
		Waitor* waitor = signal->waitor;
		signal->waitor = nullptr;
		signal->state = 0;
		return waitor;
	}();
	
	// schedule unlinked fibers
	while (waitor) {
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

void wait(BinarySignal* signal) {
	// fast path - is green
	if ((signal->state & is_red_bit) == 0) return;

	g_system->m_sync.enter();

	signal->state.setBits(signal_has_parked_bit);

	// has_parked_bit is set, so only we can change is_red_bit
	
	// if somebody set the signal green while we got here
	if (signal->state.compareExchange(0, signal_has_parked_bit)) {
		ASSERT(!signal->waitor);
		g_system->m_sync.exit();
		return;
	}

	// signal is red, park the fiber
	Waitor waitor;
	waitor.next = signal->waitor;
	waitor.fiber = getWorker()->m_current_fiber;
	signal->waitor = &waitor;
	
	// switch fibers
	FiberDecl* this_fiber = getWorker()->m_current_fiber;
	const profiler::FiberSwitchData& switch_data = profiler::beginFiberWait(signal->generation, true);
	FiberDecl* new_fiber = g_system->m_free_fibers.back();
	g_system->m_free_fibers.pop();
	if (!Fiber::isValid(new_fiber->fiber)) {
		new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
	}
	getWorker()->m_current_fiber = new_fiber;
	ASSERT(signal->state == (is_red_bit | signal_has_parked_bit));
	Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
	getWorker()->m_current_fiber = this_fiber;
	g_system->m_sync.exit();
	profiler::endFiberWait(switch_data);
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

void addCounter(Signal* signal, u32 value) {
	i32 counter = signal->counter;

	// fast path - counter is not locked
	if ((counter & signal_has_parked_bit) == 0 && signal->counter.compareExchange(counter + value, counter)) {
		if (counter == 0) {
			signal->generation = g_generation.inc();
		}
		return;
	}

	// slow path
	Lumix::MutexGuard lock(g_system->m_sync);

	// set the parked bit so nobody else can change the counter
	signal->counter.setBits(signal_has_parked_bit);
	// add value
	counter = signal->counter.add(value);
	// clear the parked bit if noone is waiting
	if (!signal->waitor) {
		signal->counter = (counter + value) & ~signal_has_parked_bit;
		ASSERT(signal->counter >= 0); 
	}
}

// decrement signal counter
// if new value == 0, schedule all fibers waiting on a signal
LUMIX_FORCE_INLINE static void trigger(Signal* signal) {
	PROFILE_FUNCTION();

	i32 counter = signal->counter;
	// fast path - no one is waiting
	if ((counter & signal_has_parked_bit) == 0 && signal->counter.compareExchange(counter - 1, counter)) return;

	// spin a bit
	for (u32 i = 0; i < 10; ++i) {
		counter = signal->counter;
		if ((counter & signal_has_parked_bit) == 0 && signal->counter.compareExchange(counter - 1, counter)) return;
	}

	Waitor* waitor = nullptr;
	// slow path
	{
		Lumix::MutexGuardProfiled lock(g_system->m_sync);

		// set the parked bit so nobody else can change the counter
		signal->counter.setBits(signal_has_parked_bit);
		
		// decrement signal counter
		counter = signal->counter.dec() - 1;
		ASSERT(counter >= 0);
		if (!signal->waitor) {
			// no one is waiting
			// clear the parked bit
			const u32 new_value = counter & ~signal_has_parked_bit;
			signal->counter = new_value;
			ASSERT(signal->counter >= 0);
			return;
		}

		// if we have green signal
		if (signal->counter == signal_has_parked_bit) {
			// pop the wait queue
			waitor = signal->waitor;
			signal->waitor = nullptr;
			signal->counter = counter & ~signal_has_parked_bit;
			ASSERT(signal->counter >= 0);
		}
	}

	if (!waitor) return;

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

void wait(Signal* signal) {
	PROFILE_FUNCTION();

	// fast path
	if (signal->counter == 0) return;

	// slow path
	g_system->m_sync.enter();

	if (!getWorker()) {
		while (signal->counter > 0) {
			g_system->m_sync.exit();
			os::sleep(1);
			g_system->m_sync.enter();
		}
		g_system->m_sync.exit();
		return;
	}

	// set the parked bit so nobody else can change the counter
	signal->counter.setBits(signal_has_parked_bit);

	// we checked for green signal in fast path, but it could have changed in the meantime
	if (signal->counter == signal_has_parked_bit) {
		// signal is green
		ASSERT(!signal->waitor);
		signal->counter = 0;
		g_system->m_sync.exit();
		return;
	}

	// signal is red, park the fiber
	FiberDecl* this_fiber = getWorker()->m_current_fiber;

	Waitor waitor;
	waitor.fiber = this_fiber;
	waitor.next = signal->waitor;
	signal->waitor = &waitor;

	const profiler::FiberSwitchData& switch_data = profiler::beginFiberWait(signal->generation, false);
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
		Work work;
		while (!worker->m_finished && !tryPopWork(work, worker)) {}
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
				trigger(work.job.dec_on_finish);
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

void enter(Mutex* mutex) {
	ASSERT(getWorker());
	for (;;) {
		// fast path - if not locked and no one is waiting, lock it
		const bool is_locked = mutex->state & is_locked_bit;
		if (!is_locked && mutex->state.compareExchange(is_locked_bit, 0)) return;

		// spin a bit
		for (u32 i = 0; i < 40; ++i) {
			// do not spin if someone is already waiting
			if (mutex->state & mutex_has_parked_bit) break;
			// try to lock
			if (mutex->state.compareExchange(is_locked_bit, 0)) return;
		}

		// let's park the fiber
		g_system->m_sync.enter();

		// set the parked bit so noone can change is_locked_bit
		mutex->state.setBits(mutex_has_parked_bit);
		
		// at this point, has_parked_bit is set, and noone but us can clear it (since we are holding `m_sync`)
		// and since has_parked_bit_is set, noone can change is_locked_bit either
		
		// is is_locked_bit is not set, we can lock the mutex
		if (mutex->state.compareExchange(mutex->waitor ? mutex_has_parked_bit | is_locked_bit : is_locked_bit, mutex_has_parked_bit)) {
			// we manged to lock the mutex
			g_system->m_sync.exit();
			return;
		}

		// we made sure parked bit is set, and then we made sure locked bit is set
		// so until we release `m_sync`, only we can change the state of the mutex

		Waitor waitor;
		FiberDecl* this_fiber = getWorker()->m_current_fiber;
		waitor.fiber = this_fiber;
		waitor.next = mutex->waitor;
		mutex->waitor = &waitor;

		// switch fibers
		mutex->generation = g_generation.inc();
		const profiler::FiberSwitchData& switch_data = profiler::beginFiberWait(mutex->generation, true);
		FiberDecl* new_fiber = g_system->m_free_fibers.back();
		g_system->m_free_fibers.pop();
		if (!Fiber::isValid(new_fiber->fiber)) {
			new_fiber->fiber = Fiber::create(64 * 1024, manage, new_fiber);
		}
		getWorker()->m_current_fiber = new_fiber;
		ASSERT(mutex->state == (is_locked_bit | mutex_has_parked_bit));
		Fiber::switchTo(&this_fiber->fiber, new_fiber->fiber);
		getWorker()->m_current_fiber = this_fiber;
		g_system->m_sync.exit();
		profiler::endFiberWait(switch_data);
		// somebody exited the mutex, let's try to lock again
		// somebody else might have been faster, so we need to do the whole checking again
	}
}

void exit(Mutex* mutex) {
	ASSERT(getWorker());
	ASSERT(mutex->state & is_locked_bit);

	// fast path - if no one is waiting, just unlock the mutex
	if (mutex->state.compareExchange(0, is_locked_bit)) return;

	// since fast path failed, there must be at least one fiber waiting
	// and only we can push it to the queue
	ASSERT(mutex->state == (is_locked_bit | mutex_has_parked_bit));

	// slow path - unpark one waiting fiber, so it can try to enter the mutex
	Lumix::MutexGuard guard(g_system->m_sync);
	
	Waitor* waitor = mutex->waitor;
	// pop one waitor
	mutex->waitor = waitor->next;
	
	// push it to a queue
	const u8 worker_idx = waitor->fiber->current_job.worker_index;
	if (worker_idx == ANY_WORKER) {
		g_system->m_work_queue.push(waitor->fiber);
	}
	else {
		WorkerTask* worker = g_system->m_workers[worker_idx % g_system->m_workers.size()];
		worker->m_work_queue.push(waitor->fiber);
	}

	// clear the locked bit, clear also the parked bit if no one is left waiting
	bool cleared = mutex->state.compareExchange(mutex->waitor ? mutex_has_parked_bit : 0, is_locked_bit | mutex_has_parked_bit);
	ASSERT(cleared);
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

void run(void* data, void(*task)(void*), Signal* on_finished, u8 worker_index)
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

void runN(void* data, void(*task)(void*), Signal* on_finished, u8 worker_index, u32 num_jobs)
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

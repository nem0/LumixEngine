#pragma once
#include "allocator.h"
#include "atomic.h"
#include "core.h"

namespace black {

struct IAllocator;

namespace jobs {

constexpr u8 ANY_WORKER = 0xff;

// can be in two states: red and green, red signal blocks wait() callers, green does not
struct Signal;

// same as OS mutex, but can be used in Job System context
struct Mutex;

// has numeric value, which is incremented by running a job, and decremented when job is done
// if the value is 0, counter is green, otherwise it's red
struct Counter;

// turn signal red from whatevere state it's in
BLACK_CORE_API void turnRed(Signal* signal);
// turn signal green from whatever state it's in, all waiting fibers are scheduled to execute
BLACK_CORE_API void turnGreen(Signal* signal);
// wait for signal to become green, or continues if it's already green, does not change state of the signal
BLACK_CORE_API void wait(Signal* signal);
// wait for signal to become green and turn it red, then continue
// if several fibers are waiting on the same signal, and the signal turns green, only one of the waiting fibers will continue
BLACK_CORE_API void waitAndTurnRed(Signal* signal);

BLACK_CORE_API void wait(Counter* counter);

BLACK_CORE_API void enter(Mutex* mutex);
BLACK_CORE_API void exit(Mutex* mutex);

BLACK_CORE_API bool init(u8 workers_count, IAllocator& allocator);
BLACK_CORE_API IAllocator& getAllocator();
BLACK_CORE_API void shutdown();
BLACK_CORE_API u8 getWorkersCount();

// yield current job and push it to worker queue
BLACK_CORE_API void moveJobToWorker(u8 worker_index);
// yield current job, push it to global queue
BLACK_CORE_API void yield();

// run single job, increment on_finished counter, decrement it when job is done
BLACK_CORE_API void run(void* data, void(*task)(void*), Counter* on_finish, u8 worker_index = ANY_WORKER);
// same as calling `run` `num_jobs` times, except it's faster
BLACK_CORE_API void runN(void* data, void(*task)(void*), Counter* on_finish, u32 num_jobs);

// spawn as many jobs as there are worker threads, and call `f`
template <typename F> void runOnWorkers(const F& f);

// same as run, but uses lambda instead of function and data pointer
// it can allocate memory for lambda, if the lambda is too big to fit in pointer
template <typename F> void runLambda(F&& f, Counter* on_finish, u8 worker = ANY_WORKER);

// call F for each element in range [0, `count`) in steps of `step`
// F is called in parallel
template <typename F> void forEach(u32 count, u32 step, const F& f);

// RAII mutex guard
struct MutexGuard;

// implementation
struct MutexGuard {
	MutexGuard(Mutex& mutex) : mutex(mutex) { enter(&mutex); }
	~MutexGuard() { exit(&mutex); }

	Mutex& mutex;
};

struct Signal {
	Signal() {}
	
	// stores locked bit in the first bit, and pointer to instrusive linked list of waiting fibers in the rest
	AtomicI64 state = 0;
	// used in profiler to identify matching red/green pairs, every times signal is turns from green to red, generation is changed
	u32 generation = 0;

private:
	Signal(const Signal&) = delete;
	Signal(Signal&&) = delete;
};

struct Mutex {
	Signal signal;
};

struct Counter {
	Signal signal;
};

template <typename F>
void runLambda(F&& f, Counter* on_finish, u8 worker) {
	void* arg;
	if constexpr (sizeof(f) == sizeof(void*) && __is_trivially_copyable(F)) {
		memcpy(&arg, &f, sizeof(arg));
		run(arg, [](void* arg){
			F* f = (F*)&arg;
			(*f)();
		}, on_finish, worker);
	}
	else {
		F* tmp = BLACK_NEW(getAllocator(), F)(static_cast<F&&>(f));
		run(tmp, [](void* arg){
			F* f = (F*)arg;
			(*f)();
			BLACK_DELETE(getAllocator(), f);
		}, on_finish, worker);

	}
}


template <typename F>
void runOnWorkers(const F& f)
{
	Counter counter;
	jobs::runN((void*)&f, [](void* data){
		(*(const F*)data)();
	}, &counter, getWorkersCount() - 1);
	f();
	wait(&counter);
}


template <typename F>
void forEach(u32 count, u32 step, const F& f) {
	if (count == 0) return;
	if (count <= step) {
		f(0, count);
		return;
	}

	const u32 steps = (count + step - 1) / step;
	const u32 num_workers = u32(getWorkersCount());
	const u32 num_jobs = steps > num_workers ? num_workers : steps;
	
	Counter counter;
	struct Data {
		const F* f;
		AtomicI32 offset = 0;
		u32 step;
		u32 count;
	} data = {
		.f = &f,
		.step = step,
		.count = count
	};
	
	ASSERT(num_jobs > 1);
	jobs::runN((void*)&data, [](void* user_ptr){
		Data* data = (Data*)user_ptr;
		const u32 count = data->count;
		const u32 step = data->step;
		const F* f = data->f;

		for(;;) {
			const i32 idx = data->offset.add(step);
			if ((u32)idx >= count) break;
			u32 to = idx + step;
			to = to > count ? count : to;
			(*f)(idx, to);
		}
	}, &counter, num_jobs - 1);

	for (;;) {
		const i32 idx = data.offset.add(step);
		if ((u32)idx >= count) break;
		u32 to = idx + step;
		to = to > count ? count : to;
		f(idx, to);
	}			

	jobs::wait(&counter);
}

} // namespace jobs

} // namespace black
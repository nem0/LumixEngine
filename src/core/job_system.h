#pragma once
#include "core.h"
#include "allocator.h"
#include "atomic.h"

namespace Lumix {

struct IAllocator;

namespace jobs {

constexpr u8 ANY_WORKER = 0xff;

struct Mutex;
struct Signal;

LUMIX_CORE_API bool init(u8 workers_count, IAllocator& allocator);
LUMIX_CORE_API IAllocator& getAllocator();
LUMIX_CORE_API void shutdown();
LUMIX_CORE_API u8 getWorkersCount();

// yield current job and push it to worker queue
LUMIX_CORE_API void moveJobToWorker(u8 worker_index);
// yield current job, push it to global queue
LUMIX_CORE_API void yield();

LUMIX_CORE_API void enter(Mutex* mutex);
LUMIX_CORE_API void exit(Mutex* mutex);

// avoid mixing red/green signals with job counter signals
// red signal blocks wait() callers
LUMIX_CORE_API void setRed(Signal* signal);
// green signal does not block wait() callers
LUMIX_CORE_API void setGreen(Signal* signal);
// wait for signal to become green, or continues if it's already green
LUMIX_CORE_API void wait(Signal* signal);

// run single job, increment on_finished signal counter, decrement it when job is done
LUMIX_CORE_API void run(void* data, void(*task)(void*), Signal* on_finish, u8 worker_index = ANY_WORKER);
// optimized batch run multi jobs, useful for foreach and others
LUMIX_CORE_API void runN(void* data, void(*task)(void*), Signal* on_finish, u8 worker_index, u32 num_jobs);

// same as run, but uses lambda instead of function and data pointer
// it can allocate memory for lambda, if the lambda is too big to fit in pointer
template <typename F>
void runLambda(F&& f, Signal* on_finish, u8 worker = ANY_WORKER) {
	void* arg;
	if constexpr (sizeof(f) == sizeof(void*) && __is_trivially_copyable(F)) {
		memcpy(&arg, &f, sizeof(arg));
		run(arg, [](void* arg){
			F* f = (F*)&arg;
			(*f)();
		}, on_finish, worker);
	}
	else {
		F* tmp = LUMIX_NEW(getAllocator(), F)(static_cast<F&&>(f));
		run(tmp, [](void* arg){
			F* f = (F*)arg;
			(*f)();
			LUMIX_DELETE(getAllocator(), f);
		}, on_finish, worker);

	}
}

// RAII mutex guard
struct MutexGuard {
	MutexGuard(Mutex& mutex) : mutex(mutex) { enter(&mutex); }
	~MutexGuard() { exit(&mutex); }

	Mutex& mutex;
};

struct Signal {
	Signal() {}
	Signal(const Signal&) = delete;
	Signal(Signal&&) = delete;
	~Signal() {
		ASSERT(!waitor);
		ASSERT(!(i32)counter);
	}

	struct Waitor* waitor = nullptr;
	AtomicI32 counter = 0;
	i32 generation;
};

struct Mutex {
	AtomicI32 state = 0;
	Waitor* waitor = nullptr;
	u32 generation = 0;
};

template <typename F>
void runOnWorkers(const F& f)
{
	Signal signal;
	jobs::runN((void*)&f, [](void* data){
		(*(const F*)data)();
	}, &signal, ANY_WORKER, getWorkersCount() - 1);
	f();
	wait(&signal);
}


// call F for each element in range [0, `count`) in steps of `step`
// F is called in parallel
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
	
	struct Data {
		const F* f;
		Signal signal;
		AtomicI32 countdown;
		AtomicI32 offset = 0;
		u32 step;
		u32 count;
	} data = {
		.f = &f,
		.countdown = AtomicI32(num_jobs - 1),
		.step = step,
		.count = count
	};
	
	jobs::setRed(&data.signal);
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
		if (data->countdown.dec() == 1) {
			jobs::setGreen(&data->signal);
		}
	}, nullptr, ANY_WORKER, num_jobs - 1);

	for (;;) {
		const i32 idx = data.offset.add(step);
		if ((u32)idx >= count) break;
		u32 to = idx + step;
		to = to > count ? count : to;
		f(idx, to);
	}			
	
	wait(&data.signal);
}

} // namespace jobs

} // namespace Lumix
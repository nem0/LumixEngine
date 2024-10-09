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

LUMIX_CORE_API void setRed(Signal* signal);
LUMIX_CORE_API void setGreen(Signal* signal);
LUMIX_CORE_API void wait(Signal* signal);

LUMIX_CORE_API void run(void* data, void(*task)(void*), Signal* on_finish, u8 worker_index = ANY_WORKER);

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

struct MutexGuard {
	MutexGuard(Mutex& mutex) : mutex(mutex) { enter(&mutex); }
	~MutexGuard() { exit(&mutex); }

	Mutex& mutex;
};

struct Signal {
	~Signal() { ASSERT(!waitor); ASSERT(!counter); }

	struct Waitor* waitor = nullptr;
	AtomicI32 counter = 0;
	i32 generation; // identify different red-green pairs on the same signal, used by profiler
};

struct Mutex {
	Signal signal; // do not access this outside of job_system.cpp
};

template <typename F>
void runOnWorkers(const F& f)
{
	Signal signal;
	for(int i = 1, c = getWorkersCount(); i < c; ++i) {
		jobs::run((void*)&f, [](void* data){
			(*(const F*)data)();
		}, &signal);
	}
	f();
	wait(&signal);
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
	
	Signal signal;
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
	
	for (u32 i = 1; i < num_jobs; ++i) {
		jobs::run((void*)&data, [](void* user_ptr){
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
		}, &signal);
	}

	for (;;) {
		const i32 idx = data.offset.add(step);
		if ((u32)idx >= count) break;
		u32 to = idx + step;
		to = to > count ? count : to;
		f(idx, to);
	}			
	
	wait(&signal);
}

} // namespace jobs

} // namespace Lumix
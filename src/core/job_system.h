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

LUMIX_CORE_API void enableBackupWorker(bool enable);
// yield current job and push it to worker queue
LUMIX_CORE_API void moveJobToWorker(u8 worker_index);
// yield current job, push it to global queue
LUMIX_CORE_API void yield();

LUMIX_CORE_API void enter(Mutex* mutex);
LUMIX_CORE_API void exit(Mutex* mutex);

LUMIX_CORE_API void setRed(Signal* signal);
LUMIX_CORE_API void setGreen(Signal* signal);

LUMIX_CORE_API void run(void* data, void(*task)(void*), Signal* on_finish);
LUMIX_CORE_API void runEx(void* data, void (*task)(void*), Signal* on_finish, u8 worker_index);
LUMIX_CORE_API void wait(Signal* signal);

template <typename F>
void runLambda(F&& f, Signal* on_finish, u8 worker = ANY_WORKER) {
	void* arg;
	if constexpr (sizeof(f) == sizeof(void*) && __is_trivially_copyable(F)) {
		memcpy(&arg, &f, sizeof(arg));
		runEx(arg, [](void* arg){
			F* f = (F*)&arg;
			(*f)();
		}, on_finish, worker);
	}
	else {
		F* tmp = LUMIX_NEW(getAllocator(), F)(static_cast<F&&>(f));
		runEx(tmp, [](void* arg){
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
void forEach(i32 count, i32 step, const F& f)
{
	if (count == 0) return;
	if (count <= step) {
		f(0, count);
		return;
	}

	AtomicI32 offset = 0;

	jobs::runOnWorkers([&](){
		for(;;) {
			const i32 idx = offset.add(step);
			if (idx >= count) break;
			i32 to = idx + step;
			to = to > count ? count : to;
			f(idx, to);
		}
	});
}

} // namespace jobs

} // namespace Lumix
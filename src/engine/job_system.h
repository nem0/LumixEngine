#pragma once
#include "lumix.h"

namespace Lumix {

struct IAllocator;

namespace jobs {

constexpr u8 ANY_WORKER = 0xff;
constexpr u32 INVALID_HANDLE = 0xffFFffFF;

struct Mutex;
struct Signal;

LUMIX_ENGINE_API bool init(u8 workers_count, IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();
LUMIX_ENGINE_API u8 getWorkersCount();

LUMIX_ENGINE_API void enableBackupWorker(bool enable);

LUMIX_ENGINE_API void enter(Mutex* mutex);
LUMIX_ENGINE_API void exit(Mutex* mutex);

LUMIX_ENGINE_API void setRed(Signal* signal);
LUMIX_ENGINE_API void setGreen(Signal* signal);

LUMIX_ENGINE_API void run(void* data, void(*task)(void*), Signal* on_finish);
LUMIX_ENGINE_API void runEx(void* data, void (*task)(void*), Signal* on_finish, u8 worker_index);
LUMIX_ENGINE_API void wait(Signal* signal);


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

	volatile i32 offset = 0;

	jobs::runOnWorkers([&](){
		for(;;) {
			const i32 idx = atomicAdd(&offset, step);
			if (idx >= count) break;
			i32 to = idx + step;
			to = to > count ? count : to;
			f(idx, to);
		}
	});
}

struct MutexGuard {
	MutexGuard(Mutex& mutex) : mutex(mutex) { enter(&mutex); }
	~MutexGuard() { exit(&mutex); }

	Mutex& mutex;
};

struct Signal {
	volatile i32 counter = 0;
	struct Waitor* waitor = nullptr;
};

struct Mutex {
	volatile i32 lock = 0;
	Signal signal;
};

} // namespace jobs

} // namespace Lumix
#pragma once
#include "lumix.h"

namespace Lumix {

struct IAllocator;

namespace jobs {

using SignalHandle = u32;
constexpr u8 ANY_WORKER = 0xff;
constexpr u32 INVALID_HANDLE = 0xffFFffFF;

LUMIX_ENGINE_API bool init(u8 workers_count, IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();
LUMIX_ENGINE_API u8 getWorkersCount();

LUMIX_ENGINE_API void enableBackupWorker(bool enable);

LUMIX_ENGINE_API void incSignal(SignalHandle* signal);
LUMIX_ENGINE_API void decSignal(SignalHandle signal);

LUMIX_ENGINE_API void run(void* data, void(*task)(void*), SignalHandle* on_finish);
LUMIX_ENGINE_API void runEx(void* data, void (*task)(void*), SignalHandle* on_finish, SignalHandle precondition, u8 worker_index);
LUMIX_ENGINE_API void wait(SignalHandle waitable);


template <typename F>
void runOnWorkers(const F& f)
{
	SignalHandle signal = jobs::INVALID_HANDLE;
	for(int i = 1, c = getWorkersCount(); i < c; ++i) {
		jobs::run((void*)&f, [](void* data){
			(*(const F*)data)();
		}, &signal);
	}
	f();
	wait(signal);
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

} // namespace jobs

} // namespace Lumix
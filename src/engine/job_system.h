#pragma once
#include "lumix.h"

namespace Lumix {

struct IAllocator;

namespace JobSystem {

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
LUMIX_ENGINE_API inline bool isValid(SignalHandle waitable) { return waitable != INVALID_HANDLE; }


template <typename F>
void runOnWorkers(const F& f)
{
	SignalHandle signal = JobSystem::INVALID_HANDLE;
	for(int i = 0, c = getWorkersCount(); i < c; ++i) {
		JobSystem::run((void*)&f, [](void* data){
			(*(const F*)data)();
		}, &signal);
	}
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

	struct Data {
		const F* f;
		volatile i32 offset = 0;
		i32 count;
		i32 step;
	} data;
	data.count = count;
	data.step = step;
	data.f = &f;
	
	SignalHandle signal = JobSystem::INVALID_HANDLE;
	for(i32 i = 0; i < count; ++i) {
		JobSystem::run(&data, [](void* ptr){
			Data& data = *(Data*)ptr;
			for(;;) {
				const i32 idx = atomicAdd(&data.offset, data.step);
				if (idx >= data.count) break;
				(*data.f)(idx, minimum(idx + data.step, data.count));
			}
		}, &signal);
	}
	wait(signal);
}

} // namespace JobSystem

} // namespace Lumix
#pragma once


#include "lumix.h"


namespace Lumix
{


struct IAllocator;


namespace JobSystem
{


using SignalHandle = u32;
constexpr u8 ANY_WORKER = 0xff;
constexpr u32 INVALID_HANDLE = 0xffFFffFF;

LUMIX_ENGINE_API bool init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();
LUMIX_ENGINE_API int getWorkersCount();

LUMIX_ENGINE_API void enableBackupWorker(bool enable);

LUMIX_ENGINE_API void incSignal(SignalHandle* signal);
LUMIX_ENGINE_API void decSignal(SignalHandle signal);

LUMIX_ENGINE_API void run(void* data, void(*task)(void*), SignalHandle* on_finish);
LUMIX_ENGINE_API void runEx(void* data, void (*task)(void*), SignalHandle* on_finish, SignalHandle precondition, u8 worker_index);
LUMIX_ENGINE_API void wait(SignalHandle waitable);
LUMIX_ENGINE_API inline bool isValid(SignalHandle waitable) { return waitable != INVALID_HANDLE; }


template <typename F>
void runAsJobs(F& f)
{
    SignalHandle signal = JobSystem::INVALID_HANDLE;
    for(int i = 0, c = getWorkersCount(); i < c; ++i) {
        JobSystem::run(&f, [](void* data){
            (*(F*)data)();
        }, &signal);
    }
    wait(signal);
}


} // namespace JobSystem


} // namespace Lumix
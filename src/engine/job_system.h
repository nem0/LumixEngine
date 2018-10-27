#pragma once


#include "lumix.h"


namespace Lumix
{


struct IAllocator;


namespace JobSystem
{


using SignalHandle = u32;
enum { INVALID_HANDLE = 0xffFFffFF };

LUMIX_ENGINE_API bool init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();

LUMIX_ENGINE_API SignalHandle run(void* data, void (*task)(void*), SignalHandle precondition);
LUMIX_ENGINE_API void wait(SignalHandle waitable);
LUMIX_ENGINE_API inline bool isValid(SignalHandle waitable) { return waitable != INVALID_HANDLE; }

LUMIX_ENGINE_API SignalHandle mergeSignals(const SignalHandle* signals, int count);
LUMIX_ENGINE_API SignalHandle createSignal();
LUMIX_ENGINE_API void trigger(SignalHandle handle);
LUMIX_ENGINE_API void run(SignalHandle trigger_on_finish, void* data, void (*task)(void*));


} // namespace JobSystem


} // namespace Lumix
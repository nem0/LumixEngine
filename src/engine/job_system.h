#pragma once


#include "engine/iallocator.h"


namespace Lumix
{


namespace JobSystem
{


using CounterHandle = u32;
enum { INVALID_HANDLE = 0xffFFffFF };

LUMIX_ENGINE_API bool init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();

LUMIX_ENGINE_API CounterHandle run(void* data, void (*task)(void*));
LUMIX_ENGINE_API CounterHandle runAfter(void* data, void (*task)(void*), CounterHandle counter);
LUMIX_ENGINE_API void wait(CounterHandle counter);
LUMIX_ENGINE_API inline bool isValid(CounterHandle counter) { return counter != INVALID_HANDLE; }

LUMIX_ENGINE_API CounterHandle mergeCounters(const CounterHandle* counters, int count);
LUMIX_ENGINE_API CounterHandle allocateCounter();
LUMIX_ENGINE_API void decCounter(CounterHandle handle);
LUMIX_ENGINE_API void run(CounterHandle counter, void* data, void (*task)(void*));



} // namespace JobSystem


} // namespace Lumix
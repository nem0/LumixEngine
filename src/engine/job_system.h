#pragma once


#include "lumix.h"


namespace Lumix
{


struct IAllocator;


namespace JobSystem
{


using WaitableHandle = u32;
enum { INVALID_HANDLE = 0xffFFffFF };

LUMIX_ENGINE_API bool init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();

LUMIX_ENGINE_API WaitableHandle run(void* data, void (*task)(void*));
LUMIX_ENGINE_API WaitableHandle runAfter(void* data, void (*task)(void*), WaitableHandle precondition);
LUMIX_ENGINE_API void wait(WaitableHandle waitable);
LUMIX_ENGINE_API inline bool isValid(WaitableHandle waitable) { return waitable != INVALID_HANDLE; }

LUMIX_ENGINE_API WaitableHandle mergeWaitables(const WaitableHandle* waitables, int count);
LUMIX_ENGINE_API WaitableHandle allocateWaitable();
LUMIX_ENGINE_API void decCounter(WaitableHandle handle);
LUMIX_ENGINE_API void run(WaitableHandle decrement_on_finish, void* data, void (*task)(void*));



} // namespace JobSystem


} // namespace Lumix
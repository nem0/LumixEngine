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

LUMIX_ENGINE_API void run(void* data, void (*task)(void*), SignalHandle* on_finish, SignalHandle precondition);
LUMIX_ENGINE_API void wait(SignalHandle waitable);
LUMIX_ENGINE_API inline bool isValid(SignalHandle waitable) { return waitable != INVALID_HANDLE; }


} // namespace JobSystem


} // namespace Lumix
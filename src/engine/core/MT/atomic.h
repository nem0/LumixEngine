#pragma once

#include "lumix.h"

namespace Lumix
{
namespace MT
{


LUMIX_ENGINE_API int32 atomicIncrement(int32 volatile* value);
LUMIX_ENGINE_API int32 atomicDecrement(int32 volatile* value);
LUMIX_ENGINE_API int32 atomicAdd(int32 volatile* addend, int32 value);
LUMIX_ENGINE_API int32 atomicSubtract(int32 volatile* addend,
										int32 value);
LUMIX_ENGINE_API bool compareAndExchange(int32 volatile* dest, int32 exchange, int32 comperand);
LUMIX_ENGINE_API bool compareAndExchange64(int64 volatile* dest, int64 exchange, int64 comperand);
LUMIX_ENGINE_API bool memoryBarrier();


} // ~namespace MT
} // ~namespace Lumix

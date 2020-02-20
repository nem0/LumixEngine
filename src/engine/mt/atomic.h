#pragma once

#include "engine/lumix.h"

namespace Lumix
{
namespace MT
{


LUMIX_ENGINE_API i64 atomicIncrement(i64 volatile* value);
LUMIX_ENGINE_API i32 atomicIncrement(i32 volatile* value);
// returns the resulting value
LUMIX_ENGINE_API i32 atomicDecrement(i32 volatile* value);
// returns the initial value
LUMIX_ENGINE_API i32 atomicAdd(i32 volatile* addend, i32 value);
LUMIX_ENGINE_API i32 atomicSubtract(i32 volatile* addend, i32 value);
LUMIX_ENGINE_API bool compareAndExchange(i32 volatile* dest, i32 exchange, i32 comperand);
LUMIX_ENGINE_API bool compareAndExchange64(i64 volatile* dest, i64 exchange, i64 comperand);
LUMIX_ENGINE_API void memoryBarrier();


} // namespace MT
} // namespace Lumix

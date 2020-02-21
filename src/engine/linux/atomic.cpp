#include "engine/atomic.h"


namespace Lumix
{

i32 atomicIncrement(i32 volatile* value)
{
	return __sync_fetch_and_add(value, 1) + 1;
}

i64 atomicIncrement(i64 volatile* value)
{
	return __sync_fetch_and_add(value, 1) + 1;
}

i32 atomicDecrement(i32 volatile* value)
{
	return __sync_fetch_and_sub(value, 1) - 1;
}

i32 atomicAdd(i32 volatile* addend, i32 value)
{
	return __sync_fetch_and_add(addend, value) + value;
}

i32 atomicSubtract(i32 volatile* addend, i32 value)
{
	return __sync_fetch_and_sub(addend, value) - value;
}

bool compareAndExchange(i32 volatile* dest, i32 exchange, i32 comperand)
{
	return __sync_bool_compare_and_swap(dest, comperand, exchange);
}

bool compareAndExchange64(i64 volatile* dest, i64 exchange, i64 comperand)
{
	return __sync_bool_compare_and_swap(dest, comperand, exchange);
}


LUMIX_ENGINE_API void memoryBarrier()
{
	__sync_synchronize();
}


} // namespace Lumix

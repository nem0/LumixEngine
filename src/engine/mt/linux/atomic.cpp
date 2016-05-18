#include "engine/mt/atomic.h"


namespace Lumix
{
namespace MT
{

int32 atomicIncrement(int32 volatile* value)
{
	return __sync_fetch_and_add(value, 1) + 1;
}

int32 atomicDecrement(int32 volatile* value)
{
	return __sync_fetch_and_sub(value, 1) - 1;
}

int32 atomicAdd(int32 volatile* addend, int32 value)
{
	return __sync_fetch_and_add(addend, value) + value;
}

int32 atomicSubtract(int32 volatile* addend, int32 value)
{
	return __sync_fetch_and_sub(addend, value) - value;
}

bool compareAndExchange(int32 volatile* dest, int32 exchange, int32 comperand)
{
	return __sync_bool_compare_and_swap(dest, comperand, exchange);
}

bool compareAndExchange64(int64 volatile* dest, int64 exchange, int64 comperand)
{
	return __sync_bool_compare_and_swap(dest, comperand, exchange);
}


LUMIX_ENGINE_API void memoryBarrier()
{
	__sync_synchronize();
}


} // ~namespace MT
} // ~namespace Lumix

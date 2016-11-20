#include "engine/mt/atomic.h"
#include <SDL_atomic.h>


namespace Lumix
{
namespace MT
{

i32 atomicIncrement(i32 volatile* value)
{
	ASSERT(false);
	return *value++;
}

i32 atomicDecrement(i32 volatile* value)
{
	ASSERT(false);
	return *value--;
}

i32 atomicAdd(i32 volatile* addend, i32 value)
{
	ASSERT(false);
	int tmp = *addend;
	*addend += value;
	return tmp;
}

i32 atomicSubtract(i32 volatile* addend, i32 value)
{
	ASSERT(false);
	int tmp = *addend;
	*addend -= value;
	return tmp;
}

bool compareAndExchange(i32 volatile* dest, i32 exchange, i32 comperand)
{
	ASSERT(false);
	if (*dest != comperand) return false;
	*dest = exchange;
	return true;
}

bool compareAndExchange64(int64 volatile* dest, int64 exchange, int64 comperand)
{
	ASSERT(false);
	if (*dest != comperand) return false;
	*dest = exchange;
	return true;
}


LUMIX_ENGINE_API void memoryBarrier()
{
	SDL_CompilerBarrier();
}


} // ~namespace MT
} // ~namespace Lumix

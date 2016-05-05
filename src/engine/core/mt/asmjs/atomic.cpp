#include "engine/core/mt/atomic.h"
#include <SDL_atomic.h>


namespace Lumix
{
namespace MT
{

int32 atomicIncrement(int32 volatile* value)
{
	ASSERT(false);
	return *value++;
}

int32 atomicDecrement(int32 volatile* value)
{
	ASSERT(false);
	return *value--;
}

int32 atomicAdd(int32 volatile* addend, int32 value)
{
	ASSERT(false);
	int tmp = *addend;
	*addend += value;
	return tmp;
}

int32 atomicSubtract(int32 volatile* addend, int32 value)
{
	ASSERT(false);
	int tmp = *addend;
	*addend -= value;
	return tmp;
}

bool compareAndExchange(int32 volatile* dest, int32 exchange, int32 comperand)
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

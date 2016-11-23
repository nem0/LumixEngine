#include "engine/mt/atomic.h"
#include <intrin.h>


namespace Lumix
{
namespace MT
{

i32 atomicIncrement(i32 volatile* value)
{
	return _InterlockedIncrement((volatile long*)value);
}

i32 atomicDecrement(i32 volatile* value)
{
	return _InterlockedDecrement((volatile long*)value);
}

i32 atomicAdd(i32 volatile* addend, i32 value)
{
	return _InterlockedExchangeAdd((volatile long*)addend, value);
}

i32 atomicSubtract(i32 volatile* addend, i32 value)
{
	return _InterlockedExchangeAdd((volatile long*)addend, -value);
}

bool compareAndExchange(i32 volatile* dest, i32 exchange, i32 comperand)
{
	return _InterlockedCompareExchange((volatile long*)dest, exchange, comperand) == comperand;
}

bool compareAndExchange64(i64 volatile* dest, i64 exchange, i64 comperand)
{
	return _InterlockedCompareExchange64(dest, exchange, comperand) == comperand;
}


LUMIX_ENGINE_API void memoryBarrier()
{
#ifdef _M_AMD64
	__faststorefence();
#elif defined _IA64_
	__mf();
#else
	int Barrier;
	__asm {
		xchg Barrier, eax
	}
#endif
}


} // ~namespace MT
} // ~namespace Lumix

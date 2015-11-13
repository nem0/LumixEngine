#include "core/mt/atomic.h"
#include <intrin.h>


namespace Lumix
{
namespace MT
{

int32 atomicIncrement(int32 volatile* value)
{
	return _InterlockedIncrement((volatile long*)value);
}

int32 atomicDecrement(int32 volatile* value)
{
	return _InterlockedDecrement((volatile long*)value);
}

int32 atomicAdd(int32 volatile* addend, int32 value)
{
	return _InterlockedExchangeAdd((volatile long*)addend, value);
}

int32 atomicSubtract(int32 volatile* addend, int32 value)
{
	return _InterlockedExchangeAdd((volatile long*)addend, -value);
}

bool compareAndExchange(int32 volatile* dest, int32 exchange, int32 comperand)
{
	return _InterlockedCompareExchange((volatile long*)dest, exchange, comperand) == comperand;
}

bool compareAndExchange64(int64 volatile* dest, int64 exchange, int64 comperand)
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

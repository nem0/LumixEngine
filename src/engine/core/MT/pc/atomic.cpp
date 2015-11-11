#include "core/mt/atomic.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Lumix
{
	namespace MT
	{
		int32 atomicIncrement(int32 volatile *value)
		{
			return InterlockedIncrement((LONG*)value);
		}

		int32 atomicDecrement(int32 volatile *value)
		{
			return InterlockedDecrement((LONG*)value);
		}

		int32 atomicAdd(int32 volatile *addend, int32 value)
		{
			return InterlockedExchangeAdd((LONG*)addend, value);
		}

		int32 atomicSubtract(int32 volatile *addend, int32 value)
		{
			return InterlockedExchangeAdd((LONG*)addend, -value);
		}

		bool compareAndExchange(int32 volatile* dest, int32 exchange, int32 comperand)
		{
			return InterlockedCompareExchange((LONG*)dest, exchange, comperand) == comperand;
		}

		bool compareAndExchange64(int64 volatile* dest, int64 exchange, int64 comperand)
		{
			return InterlockedCompareExchange64(dest, exchange, comperand) == comperand;
		}
	} // ~namespace MT
} // ~namespace Lumix

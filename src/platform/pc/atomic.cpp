#include "platform/atomic.h"
#include <Windows.h>

namespace Lux
{
	namespace MT
	{
		int32_t atomicIncrement(int32_t volatile *value)
		{
			return InterlockedIncrement((LONG*)value);
		}

		int32_t atomicDecrement(int32_t volatile *value)
		{
			return InterlockedDecrement((LONG*)value);
		}

		int32_t atomicAdd(int32_t volatile *addend, int32_t value)
		{
			return InterlockedExchangeAdd((LONG*)addend, value);
		}

		int32_t atomicSubtract(int32_t volatile *addend, int32_t value)
		{
			return InterlockedExchangeAdd((LONG*)addend, -value);
		}

		bool compareAndExchange(int32_t volatile* dest, int32_t exchange, int32_t comperand)
		{
			return InterlockedCompareExchange((LONG*)dest, exchange, comperand) == comperand;
		}

		bool compareAndExchange64(int64_t volatile* dest, int64_t exchange, int64_t comperand)
		{
			return InterlockedCompareExchange64(dest, exchange, comperand) == comperand;
		}
	} // ~namespace MT
} // ~namespace Lux
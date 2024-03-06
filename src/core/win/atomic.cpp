#include "core/atomic.h"
#include <intrin.h>

namespace Lumix
{

void AtomicI32::operator =(i32 v) { _InterlockedExchange((volatile long*)&value, v); }
AtomicI32::operator i32() const { return _InterlockedExchangeAdd((volatile long*)&value, 0); }

i32 AtomicI32::inc() { return _InterlockedExchangeAdd((volatile long*)&value, 1); }
i32 AtomicI32::dec() { return _InterlockedExchangeAdd((volatile long*)&value, -1); }
i32 AtomicI32::add(i32 v) { return _InterlockedExchangeAdd((volatile long*)&value, v); }
i32 AtomicI32::subtract(i32 v) { return _InterlockedExchangeAdd((volatile long*)&value, -v); }

bool AtomicI32::compareExchange(i32 exchange, i32 comperand) { 
	return _InterlockedCompareExchange((volatile long*)&value, exchange, comperand) == comperand;
}

void AtomicI64::operator =(i64 v) { _InterlockedExchange64((volatile long long*)&value, v); }
AtomicI64::operator i64() const { return _InterlockedExchangeAdd64((volatile long long*)&value, 0); }

i64 AtomicI64::inc() { return _InterlockedExchangeAdd64((volatile long long*)&value, 1); }
i64 AtomicI64::dec() { return _InterlockedExchangeAdd64((volatile long long*)&value, -1); }
i64 AtomicI64::add(i64 v) { return _InterlockedExchangeAdd64((volatile long long*)&value, v); }
i64 AtomicI64::subtract(i64 v) { return _InterlockedExchangeAdd64((volatile long long*)&value, -v); }

bool AtomicI64::compareExchange(i64 exchange, i64 comperand) { 
	return _InterlockedCompareExchange64((volatile long long*)&value, exchange, comperand) == comperand;
}

bool compareExchangePtr(volatile void** value, void* exchange, void* comperand) {
	static_assert(sizeof(comperand) == sizeof(long long));
	return _InterlockedCompareExchange64((volatile long long*)value, (long long)exchange, (long long)comperand) == (long long)comperand;
}

LUMIX_CORE_API void memoryBarrier()
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


} // namespace Lumix

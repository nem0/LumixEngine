#include "core/atomic.h"
#include <intrin.h>

extern "C" {
	void _ReadBarrier(void);
	void _WriteBarrier(void);
}

namespace black
{

void AtomicI32::operator =(i32 v) { _InterlockedExchange((volatile long*)&value, v); }
AtomicI32::operator i32() const {
	memoryBarrier();
	return value;
}

i32 AtomicI32::inc() { return _InterlockedExchangeAdd((volatile long*)&value, 1); }
i32 AtomicI32::dec() { return _InterlockedExchangeAdd((volatile long*)&value, -1); }
i32 AtomicI32::add(i32 v) { return _InterlockedExchangeAdd((volatile long*)&value, v); }
i32 AtomicI32::subtract(i32 v) { return _InterlockedExchangeAdd((volatile long*)&value, -v); }
i32 AtomicI32::setBits(i32 v) { return _InterlockedOr((volatile long*)&value, v); }
i32 AtomicI32::clearBits(i32 v) { return _InterlockedAnd((volatile long*)&value, ~v); }

bool AtomicI32::compareExchange(volatile i32* value, i32 exchange, i32 comperand) {
	return _InterlockedCompareExchange((volatile long*)value, exchange, comperand) == comperand;
}

bool AtomicI32::compareExchange(i32 exchange, i32 comperand) { 
	return _InterlockedCompareExchange((volatile long*)&value, exchange, comperand) == comperand;
}

void AtomicI64::operator =(i64 v) { _InterlockedExchange64((volatile long long*)&value, v); }

AtomicI64::operator i64() const { 
	memoryBarrier();
	return value;
}

i64 AtomicI64::inc() { return _InterlockedExchangeAdd64((volatile long long*)&value, 1); }
i64 AtomicI64::dec() { return _InterlockedExchangeAdd64((volatile long long*)&value, -1); }
i64 AtomicI64::add(i64 v) { return _InterlockedExchangeAdd64((volatile long long*)&value, v); }
i64 AtomicI64::subtract(i64 v) { return _InterlockedExchangeAdd64((volatile long long*)&value, -v); }
i64 AtomicI64::exchange(i64 new_value) { return _InterlockedExchange64((volatile long long*)&value, new_value); }
i64 AtomicI64::setBits(i64 v) { return _InterlockedOr64((volatile long long*)&value, v); }
i64 AtomicI64::clearBits(i64 v) { return _InterlockedAnd64((volatile long long*)&value, ~v); }
bool AtomicI64::bitTestAndSet(u32 bit_position) { return !_interlockedbittestandset64((long long*)&value, bit_position); }

bool AtomicI64::compareExchange(i64 exchange, i64 comperand) { 
	return _InterlockedCompareExchange64((volatile long long*)&value, exchange, comperand) == comperand;
}

bool compareExchangePtr(void*volatile* value, void* exchange, void* comperand) {
	return _InterlockedCompareExchangePointer(value, exchange, comperand) == comperand;
}

void* exchangePtr(void* volatile* value, void* exchange) {
	return _InterlockedExchangePointer(value, exchange);

}

void cpuRelax() {
	_mm_pause();
}

void readBarrier() {
	_ReadBarrier();
}

void writeBarrier() {
	_WriteBarrier();
}

void memoryBarrier()
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


} // namespace black

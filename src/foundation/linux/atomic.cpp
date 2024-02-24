#include "foundation/atomic.h"


namespace Lumix
{


void AtomicI32::operator =(i32 v) { __atomic_store_n(&value, v, __ATOMIC_RELEASE); }
AtomicI32::operator i32() const { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }

i32 AtomicI32::inc() { return __atomic_fetch_add(&value, 1, __ATOMIC_ACQ_REL); }
i32 AtomicI32::dec() { return __atomic_fetch_sub(&value, 1, __ATOMIC_ACQ_REL); }
i32 AtomicI32::add(i32 v) { return __atomic_fetch_add(&value, v, __ATOMIC_ACQ_REL); }
i32 AtomicI32::subtract(i32 v) { return __atomic_fetch_sub(&value, v, __ATOMIC_ACQ_REL); }

bool AtomicI32::compareExchange(i32 exchange, i32 comperand) { 
	return __sync_bool_compare_and_swap(&value, comperand, exchange);
}

void AtomicI64::operator =(i64 v) { __atomic_store_n(&value, v, __ATOMIC_RELEASE); }
AtomicI64::operator i64() const { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }

i64 AtomicI64::inc() { return __atomic_fetch_add(&value, 1, __ATOMIC_ACQ_REL); }
i64 AtomicI64::dec() { return __atomic_fetch_sub(&value, 1, __ATOMIC_ACQ_REL); }
i64 AtomicI64::add(i64 v) { return __atomic_fetch_add(&value, v, __ATOMIC_ACQ_REL); }
i64 AtomicI64::subtract(i64 v) { return __atomic_fetch_sub(&value, v, __ATOMIC_ACQ_REL); }

bool AtomicI64::compareExchange(i64 exchange, i64 comperand) { 
	return __sync_bool_compare_and_swap(&value, comperand, exchange);
}

bool compareExchangePtr(volatile void** value, void* exchange, void* comperand) {
	return __sync_bool_compare_and_swap(value, comperand, exchange);
}

LUMIX_FOUNDATION_API void memoryBarrier()
{
	__sync_synchronize();
}


} // namespace Lumix

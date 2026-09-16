#include "core/atomic.h"


namespace Lumix {


void AtomicI32::operator=(i32 v) {
	__atomic_store_n(&value, v, __ATOMIC_RELEASE);
}
AtomicI32::operator i32() const {
	return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
}

i32 AtomicI32::inc() {
	return __atomic_fetch_add(&value, 1, __ATOMIC_ACQ_REL);
}
i32 AtomicI32::dec() {
	return __atomic_fetch_sub(&value, 1, __ATOMIC_ACQ_REL);
}
i32 AtomicI32::add(i32 v) {
	return __atomic_fetch_add(&value, v, __ATOMIC_ACQ_REL);
}
i32 AtomicI32::subtract(i32 v) {
	return __atomic_fetch_sub(&value, v, __ATOMIC_ACQ_REL);
}
i32 AtomicI32::setBits(i32 v) {
	return __atomic_fetch_or(&value, v, __ATOMIC_ACQ_REL);
}
i32 AtomicI32::clearBits(i32 v) {
	return __atomic_fetch_and(&value, ~v, __ATOMIC_ACQ_REL);
}

bool AtomicI32::compareExchange(volatile i32* target, i32 exchange, i32 comperand) {
	return __atomic_compare_exchange_n(target, &comperand, exchange, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

bool AtomicI32::compareExchange(i32 exchange, i32 comperand) {
	return __sync_bool_compare_and_swap(&value, comperand, exchange);
}

void AtomicI64::operator=(i64 v) {
	__atomic_store_n(&value, v, __ATOMIC_RELEASE);
}
AtomicI64::operator i64() const {
	return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
}

i64 AtomicI64::inc() {
	return __atomic_fetch_add(&value, 1, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::dec() {
	return __atomic_fetch_sub(&value, 1, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::add(i64 v) {
	return __atomic_fetch_add(&value, v, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::subtract(i64 v) {
	return __atomic_fetch_sub(&value, v, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::exchange(i64 v) {
	return __atomic_exchange_n(&value, v, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::setBits(i64 v) {
	return __atomic_fetch_or(&value, v, __ATOMIC_ACQ_REL);
}
i64 AtomicI64::clearBits(i64 v) {
	return __atomic_fetch_and(&value, ~v, __ATOMIC_ACQ_REL);
}
bool AtomicI64::bitTestAndSet(u32 bit_position) {
	const i64 mask = i64(1) << bit_position;
	return (__atomic_fetch_or(&value, mask, __ATOMIC_ACQ_REL) & mask) == 0;
}

bool AtomicI64::compareExchange(i64 exchange, i64 comperand) {
	return __sync_bool_compare_and_swap(&value, comperand, exchange);
}

bool compareExchangePtr(void* volatile* value, void* exchange, void* comperand) {
	return __atomic_compare_exchange_n(value, &comperand, exchange, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void* exchangePtr(void* volatile* value, void* exchange) {
	return __atomic_exchange_n(value, exchange, __ATOMIC_ACQ_REL);
}

LUMIX_CORE_API void memoryBarrier() {
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}
LUMIX_CORE_API void readBarrier() {
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
}
LUMIX_CORE_API void writeBarrier() {
	__atomic_thread_fence(__ATOMIC_RELEASE);
}
LUMIX_CORE_API void cpuRelax() {
	__asm__ volatile("pause" ::: "memory");
}


} // namespace Lumix

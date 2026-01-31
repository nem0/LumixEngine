#pragma once

#include "core.h"

namespace black
{

struct BLACK_CORE_API AtomicI32 {
	AtomicI32(i32 v) : value(v) {}
	
	void operator =(i32 v);
	operator i32() const;
	
	// returns initial value of the variable
	i32 inc();
	i32 dec();
	i32 add(i32 v);
	i32 subtract(i32 v);
	i32 setBits(i32 v);
	i32 clearBits(i32 v);

	bool compareExchange(i32 exchange, i32 comperand);
	static bool compareExchange(volatile i32* value, i32 exchange, i32 comperand);
private:
	volatile i32 value;
};

struct BLACK_CORE_API AtomicI64 {
	AtomicI64(i64 v) : value(v) {}
	
	void operator =(i64 v);
	operator i64() const;
	
	// returns initial value of the variable
	i64 inc();
	i64 dec();
	i64 add(i64 v);
	i64 subtract(i64 v);
	i64 exchange(i64 new_value);
	i64 setBits(i64 v);
	i64 clearBits(i64 v);
	bool bitTestAndSet(u32 bit_position);

	bool compareExchange(i64 exchange, i64 comperand);
	volatile i64 value;
};

BLACK_CORE_API void* exchangePtr(void* volatile* value, void* exchange);
BLACK_CORE_API bool compareExchangePtr(void*volatile* value, void* exchange, void* comperand);
BLACK_CORE_API void memoryBarrier();
BLACK_CORE_API void readBarrier();
BLACK_CORE_API void writeBarrier();
BLACK_CORE_API void cpuRelax(); // rep nop, can be used in busy wait to conserve resources

} // namespace black

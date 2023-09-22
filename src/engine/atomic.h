#pragma once

#include "engine/lumix.h"

namespace Lumix
{

LUMIX_ENGINE_API struct AtomicI32 {
	AtomicI32(i32 v) : value(v) {}
	
	void operator =(i32 v);
	operator i32() const;
	
	// returns initial value of the variable
	i32 inc();
	i32 dec();
	i32 add(i32 v);
	i32 subtract(i32 v);

	bool compareExchange(i32 exchange, i32 comperand);
private:
	volatile i32 value;
};

LUMIX_ENGINE_API struct AtomicI64 {
	AtomicI64(i64 v) : value(v) {}
	
	void operator =(i64 v);
	operator i64() const;
	
	// returns initial value of the variable
	i64 inc();
	i64 dec();
	i64 add(i64 v);
	i64 subtract(i64 v);

	bool compareExchange(i64 exchange, i64 comperand);
private:
	volatile i64 value;
};

LUMIX_ENGINE_API bool compareExchangePtr(volatile void** value, void* exchange, void* comperand);
LUMIX_ENGINE_API void memoryBarrier();

} // namespace Lumix

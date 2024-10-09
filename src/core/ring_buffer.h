#pragma once

#include "core/array.h"
#include "core/sync.h"

namespace Lumix {

// Thread-safe, mutex-guarded ring buffer, with a fallback array for overflow
template <typename T, u32 CAPACITY>
struct RingBuffer {
	static_assert(__is_trivially_copyable(T));
	RingBuffer(IAllocator& allocator)
		: m_fallback(allocator)
	{
		static_assert(CAPACITY > 2);
	}

	LUMIX_FORCE_INLINE bool pop(T& obj) {
		MutexGuard lock(mutex);
		const bool is_ring_buffer_empty = wr == rd;
		if (is_ring_buffer_empty) {
			if (m_fallback.empty()) return false;
			
			obj = m_fallback.back();
			m_fallback.pop();
			return true;
		}

		obj = objects[rd % CAPACITY];
		++rd;
		return true;
	}

	LUMIX_FORCE_INLINE void push(const T& obj) {
		MutexGuard lock(this->mutex);
		if (wr - rd >= CAPACITY) {
			// buffer full
			m_fallback.push(obj);
			return;
		}
		
		objects[wr % CAPACITY] = obj;
		++wr;
	}

	Lumix::Mutex mutex;
	T objects[CAPACITY];
	u32 rd = 0;
	u32 wr = 0;
	Array<T> m_fallback;
};

} // namespace Lumix
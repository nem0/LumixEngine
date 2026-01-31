#pragma once

#include "core/array.h"
#include "core/sync.h"

namespace black {

template <typename T, u32 CAPACITY>
struct RingBuffer {
	struct Item {
		T value;
		volatile i32 seq;
	};

	RingBuffer(IAllocator& allocator)
		: m_fallback(allocator)
	{
		static_assert(CAPACITY > 2);
		for (u32 i = 0; i < CAPACITY; ++i) {
			objects[i].seq = i;
		}
		memoryBarrier();
	}

	BLACK_FORCE_INLINE bool pop(T& obj) {
		for (;;) {
			const i32 pos = rd;
			Item* j = &objects[pos % CAPACITY];
			const i32 seq = j->seq;
			if (seq < pos + 1) {
				// nothing to pop, try fallback
				MutexGuard guard(mutex);
				if (m_fallback.empty()) return false;
				obj = m_fallback.back();
				m_fallback.pop();
				return true;
			}
			else if (seq == pos + 1) {
				// try to pop
				if (rd.compareExchange(pos + 1, pos)) {
					obj = j->value;
					j->seq = pos + CAPACITY;
					return true;
				}
			}
			// somebody poped before us, try again
		}
	}

	BLACK_FORCE_INLINE void push(const T& obj) {
		volatile i32 pos = wr;
		Item* j;
		for (;;) {
			j = &objects[pos % CAPACITY];
			const i32 seq = j->seq;
			if (seq < pos) {
				// buffer full
				mutex.enter();
				m_fallback.push(obj);
				mutex.exit();
				return;
			}
			else if (seq == pos) {
				// we can try to push
				if (wr.compareExchange(pos + 1, pos)) break;
			}
			else {
				// somebody pushed before us, try again
				pos = wr;
			}
		}
		j->value = obj;
		j->seq = pos + 1;
	}

	Item objects[CAPACITY];
	AtomicI32 rd = 0;
	AtomicI32 wr = 0;
	Array<T> m_fallback;
	black.h::Mutex mutex;
};


} // namespace black
#pragma once

#include "core/allocator.h"
#include "core/delegate.h"

namespace black {

template <typename T> struct DelegateList;

template <typename R, typename... Args> struct DelegateList<R(Args...)> {
	explicit DelegateList(IAllocator& allocator)
		: m_allocator(allocator)
	{}

	DelegateList(DelegateList&&) = delete;
	DelegateList(const DelegateList&) = delete;
	void operator =(const DelegateList&) = delete;
	void operator =(DelegateList&&) = delete;

	~DelegateList() {
		if (m_delegates) m_allocator.deallocate(m_delegates);
	}

	template <auto Function, typename C> void bind(C* instance) {
		Delegate<R(Args...)> cb;
		cb.template bind<Function>(instance);
		push(cb);
	}

	void bindRaw(void* obj, void (*f)(void*, Args...)) {
		Delegate<R(Args...)> cb;
		cb.bindRaw(obj, f);
		push(cb);
	}
	
	void unbindRaw(void* obj, void (*f)(void*, Args...)) {
		for (u32 i = 0; i < m_size; ++i) {
			if (m_delegates[i].m_stub.first == obj && m_delegates[i].m_stub.second == f) {
				swapAndPop(i);
				break;
			}
		}
	}

	template <R (*Function)(Args...)> void bind() {
		Delegate<R(Args...)> cb;
		cb.template bind<Function>();
		push(cb);
	}

	template <R (*Function)(Args...)> void unbind() {
		Delegate<R(Args...)> cb;
		cb.template bind<Function>();
		for (u32 i = 0; i < m_size; ++i) {
			if (m_delegates[i] == cb) {
				swapAndPop(i);
				break;
			}
		}
	}

	template <auto Function, typename C> void unbind(C* instance) {
		Delegate<R(Args...)> cb;
		cb.template bind<Function>(instance);
		for (u32 i = 0; i < m_size; ++i) {
			if (m_delegates[i] == cb) {
				swapAndPop(i);
				break;
			}
		}
	}

	void invoke(Args... args) const {
		for (i32 i = 0, c = m_size; i < c; ++i) m_delegates[i].invoke(args...);
	}

	void push(Delegate<R(Args...)> value) {
		using D = Delegate<R(Args...)>;
		if (m_size == m_capacity) {
			u32 new_capacity = m_capacity < 1 ? 1 : (m_capacity + 1) * 3 / 2;
			D* new_data = (D*)m_allocator.allocate(sizeof(D) * new_capacity, alignof(D));
			if (m_delegates) {
				for (u32 i = 0; i < m_size; ++i) {
					new_data[i] = m_delegates[i];
				}
				m_allocator.deallocate(m_delegates);
			}
			m_delegates = new_data;
			m_capacity = new_capacity;
		}
		m_delegates[m_size] = value;
		++m_size;
	}

	void swapAndPop(u32 index) {
		m_delegates[index] = m_delegates[m_size - 1];
		--m_size;
	}

private:
	IAllocator& m_allocator;
	Delegate<R(Args...)>* m_delegates = nullptr;
	u32 m_size = 0;
	u32 m_capacity = 0;
};

} // namespace black

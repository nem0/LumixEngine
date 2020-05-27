#pragma once


#include "engine/array.h"
#include "engine/delegate.h"


namespace Lumix
{
template <typename T> struct DelegateList;

template <typename R, typename... Args> struct DelegateList<R(Args...)>
{
public:
	explicit DelegateList(IAllocator& allocator)
		: m_delegates(allocator)
	{
	}

	template <auto Function, typename C> void bind(C* instance)
	{
		Delegate<R(Args...)> cb;
		cb.template bind<Function>(instance);
		m_delegates.push(cb);
	}

	template <R (*Function)(Args...)> void bind()
	{
		Delegate<R(Args...)> cb;
		cb.template bind<Function>();
		m_delegates.push(cb);
	}

	template <R (*Function)(Args...)> void unbind()
	{
		Delegate<R(Args...)> cb;
		cb.template bind<Function>();
		for (int i = 0; i < m_delegates.size(); ++i)
		{
			if (m_delegates[i] == cb)
			{
				m_delegates.swapAndPop(i);
				break;
			}
		}
	}

	template <auto Function, typename C> void unbind(C* instance)
	{
		Delegate<R(Args...)> cb;
		cb.template bind<Function>(instance);
		for (int i = 0; i < m_delegates.size(); ++i)
		{
			if (m_delegates[i] == cb)
			{
				m_delegates.swapAndPop(i);
				break;
			}
		}
	}

	void invoke(Args... args)
	{
		for (i32 i = 0, c = m_delegates.size(); i < c; ++i) m_delegates[i].invoke(args...);
	}

private:
	Array<Delegate<R(Args...)>> m_delegates;
};

} // namespace Lumix

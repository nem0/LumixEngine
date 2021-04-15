#pragma once

#include "engine/lumix.h"

// base on molecular musings blogpost


namespace Lumix
{

template <typename T> struct Delegate;


template <typename R, typename... Args> struct Delegate<R(Args...)>
{
private:
	using InstancePtr = void*;
	using InternalFunction = R (*)(InstancePtr, Args...);
	struct Stub
	{
		InstancePtr first;
		InternalFunction second;
	};

	template <R (*Function)(Args...)> static R FunctionStub(InstancePtr, Args... args)
	{
		return (Function)(args...);
	}

	template <typename C, R(C::*Function)(Args...)>
	static R ClassMethodStub(InstancePtr instance, Args... args)
	{
		return (static_cast<C*>(instance)->*Function)(args...);
	}

	template <typename C, R(C::*Function)(Args...) const>
	static R ClassMethodStub(InstancePtr instance, Args... args)
	{
		return (static_cast<C*>(instance)->*Function)(args...);
	}

public:
	Delegate()
	{
		m_stub.first = nullptr;
		m_stub.second = nullptr;
	}

	bool isValid() { return m_stub.second != nullptr; }

	template <R (*Function)(Args...)> void bind()
	{
		m_stub.first = nullptr;
		m_stub.second = &FunctionStub<Function>;
	}

	template <auto F, typename C> void bind(C* instance)
	{
		m_stub.first = instance;
		m_stub.second = &ClassMethodStub<C, F>;
	}

	R invoke(Args... args) const
	{
		ASSERT(m_stub.second != nullptr);
		return m_stub.second(m_stub.first, args...);
	}

	bool operator==(const Delegate<R(Args...)>& rhs)
	{
		return m_stub.first == rhs.m_stub.first && m_stub.second == rhs.m_stub.second;
	}

private:
	Stub m_stub;
};

template <typename T> struct ToDelegate_T;
template <typename R, typename C, typename... Args> struct ToDelegate_T<R (C::*)(Args...)> {
	using Type = Delegate<R (Args...)>;
};

template <typename T> using ToDelegate = typename ToDelegate_T<T>::Type;

template <auto M, typename C>
auto makeDelegate(C* inst) {
	ToDelegate<decltype(M)> res;
	res.template bind<M, C>(inst);
	return res;
};


} // namespace Lumix

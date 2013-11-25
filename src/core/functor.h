#pragma once


namespace Lux
{


template <class TRet>
class IFunctor
{
	public:
		virtual ~IFunctor() {}

		virtual TRet operator() () = 0;
};

template <class TRet, class Arg1>
class IFunctor1
{
	public:
		virtual ~IFunctor1() {}

		virtual TRet operator() (Arg1 a) = 0;
};

template <class TRet, class Arg1, class Arg2, class Arg3>
class IFunctor3
{
	public:
		virtual ~IFunctor3() {}

		virtual TRet operator() (Arg1 a, Arg2 b, Arg3 c) = 0;
};

template <class TRet>
class Functor : public IFunctor<TRet>
{
	public:
		typedef TRet (*Function)();

	public:
		Functor(Function function)
		{
			m_function = function;
		}

		virtual TRet operator() ()
		{
			return (*m_function)();
		}

	private:
		Function m_function;
};


template <class TRet, class Arg1>
class Functor1 : public IFunctor1<TRet, Arg1>
{
	public:
		typedef TRet (*Function)(Arg1);

	public:
		Functor1(Function function)
		{
			m_function = function;
		}

		virtual TRet operator() (Arg1 a)
		{
			return (*m_function)(a);
		}

	private:
		Function	m_function;
};


template <class TRet, class Arg1, class Arg2, class Arg3>
class Functor3 : public IFunctor3<TRet, Arg1, Arg2, Arg3>
{
	public:
		typedef TRet (*Function)(Arg1, Arg2, Arg3);

	public:
		Functor3(Function function)
		{
			m_function = function;
		}

		virtual TRet operator() (Arg1 a, Arg2 b, Arg3 c)
		{
			return (*m_function)(a, b, c);
		}

	private:
		Function	m_function;
};


template <class Arg1>
class Functor1<void, Arg1> : public IFunctor1<void, Arg1>
{
	public:
		typedef void (*Function)(Arg1);

	public:
		Functor1(Function function)
		{
			m_function = function;
		}

		virtual void operator() (Arg1 a)
		{
			(*m_function)(a);
		}

	private:
		Function	m_function;
};


template <class Arg1, class Arg2, class Arg3>
class Functor3<void, Arg1, Arg2, Arg3> : public IFunctor3<void, Arg1, Arg2, Arg3>
{
	public:
		typedef void (*Function)(Arg1, Arg2, Arg3);

	public:
		Functor3(Function function)
		{
			m_function = function;
		}

		virtual void operator() (Arg1 a, Arg2 b, Arg3 c)
		{
			(*m_function)(a, b, c);
		}

	private:
		Function	m_function;
};


template <class TRet, class TObj>
class MethodFunctor : public IFunctor<TRet>
{
	public:
		typedef TRet (TObj::*Function)();

	public:
		MethodFunctor(TObj* obj, Function function)
		{
			m_function = function;
			m_obj = obj;
		}

		virtual TRet operator() ()
		{
			return (m_obj->*m_function)();
		}

	private:
		TObj*		m_obj;
		Function	m_function;
};


template <>
class Functor<void> : public IFunctor<void>
{
	public:
		typedef void (*Function)();

	public:
		Functor(Function function)
		{
			m_function = function;
		}

		virtual void operator() ()
		{
			(*m_function)();
		}

	private:
		Function m_function;
};


template <class TObj>
class MethodFunctor<void, TObj> : public IFunctor<void>
{
	public:
		typedef void (TObj::*Function)();

	public:
		MethodFunctor(TObj* obj, Function function)
		{
			m_function = function;
			m_obj = obj;
		}

		virtual void operator() ()
		{
			(m_obj->*m_function)();
		}

	private:
		TObj*		m_obj;
		Function	m_function;
};


template <class TRet, class Arg1, class Arg2, class Arg3>
IFunctor3<TRet, Arg1, Arg2, Arg3>* makeFunctor(TRet (*function)(Arg1, Arg2, Arg3))
{
	return new Functor3<TRet, Arg1, Arg2, Arg3>(function);
}


template <class TRet, class Arg1>
IFunctor1<TRet, Arg1>* makeFunctor(TRet (*function)(Arg1))
{
	return new Functor1<TRet, Arg1>(function);
}

template <class TRet, class TObj>
IFunctor<TRet>* makeFunctor(TObj* obj, TRet (TObj::*method)())
{
	return new MethodFunctor<TRet, TObj>(obj, method);
}


} // ~namespace Lux



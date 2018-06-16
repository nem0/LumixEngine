#pragma once


namespace Lumix
{


template <class T> struct RemoveReference { using Type = T; };
template <class T> struct RemoveReference<T&> { using Type = T; };
template <class T> struct RemoveReference<T&&> { using Type = T; };
template <class T> struct RemoveConst { using Type = T; };
template <class T> struct RemoveConst<const T> { using Type = T; };
template <class T> struct RemoveVolatile { using Type = T; };
template <class T> struct RemoveVolatile<volatile T> { using Type = T; };
template <class T> using RemoveCR = typename RemoveConst<typename RemoveReference<T>::Type>::Type;
template <class T> using RemoveCVR = typename RemoveVolatile<RemoveCR<T>>::Type;


template<bool B, class T = void>
struct EnableIf {};

template<class T>
struct EnableIf<true, T> { using Type = T; };

template<typename T1, typename T2>
struct IsSame
{
	enum { result = false };
};

template<typename T>
struct IsSame<T, T>
{
	enum { result = true };
};

template <int... T> struct Indices {};

template <int offset, int size, int... T>
struct BuildIndices
{
	using result = typename BuildIndices<offset, size - 1, size + offset, T...>::result;
};


template <int offset, int... T>
struct BuildIndices<offset, 0, T...>
{
	using result = Indices<T...>;
};

template <typename... Types> struct Tuple;

template <> struct Tuple<> {};

template <typename Head, typename... Tail>
struct Tuple<Head, Tail...> : Tuple<Tail...>
{
	Head value;

	Tuple() {}
	Tuple(Head head, Tail... tail) : Tuple<Tail...>(tail...), value(head) {}
	
	Tuple(const Tuple<Head, Tail...>& rhs) : Tuple<Tail...>(rhs) { value = rhs.value; }
};


template <typename T, typename C> struct TupleContains;

template <typename T, typename... Tail> 
struct TupleContains<T, Tuple<T, Tail...>>
{
	static constexpr bool value = true;
};

template <typename T>
struct TupleContains<T, Tuple<>>
{
	static constexpr bool value = false;
};


template <typename T, typename Head, typename... Tail> 
struct TupleContains<T, Tuple<Head, Tail...>> 
{
	static constexpr bool value = TupleContains<T, Tuple<Tail...>>::value;
};


template <typename... Types>
auto makeTuple(Types... types)
{
	return Tuple<Types...>(types...);
}

template<class T> struct TupleSize;

template<class... Types>
struct TupleSize<const Tuple<Types...> >
{
	enum { result = sizeof...(Types) };
};

template<class... Types>
struct TupleSize<Tuple<Types...> >
{
	enum { result = sizeof...(Types) };
};

template<int Index, class Tuple> struct TupleElement;

template<class HeadType, class... TailTypes>
struct TupleElement<0, const Tuple<HeadType, TailTypes...> >
{
	using Head = HeadType;
	using Tail = const Tuple<HeadType, TailTypes...>;
};

template<int Index, class Head, class... Tail>
struct TupleElement<Index, const Tuple<Head, Tail...> >
	: public TupleElement<Index - 1, const Tuple<Tail...> >
{
};

template<class HeadType, class... TailTypes>
struct TupleElement<0, Tuple<HeadType, TailTypes...> >
{
	using Head = HeadType;
	using Tail = Tuple<HeadType, TailTypes...>;
};

template<int Index, class Head, class... Tail>
struct TupleElement<Index, Tuple<Head, Tail...> >
	: public TupleElement<Index - 1, Tuple<Tail...> >
{
};


template<int Index, typename... Types>
constexpr auto& get(const Tuple<Types...>& tuple)
{
	using Subtuple = typename TupleElement<Index, const ::Lumix::Tuple<Types...> >::Tail;
	return (((Subtuple&)tuple).value);
}


template<int Index, typename... Types>
constexpr auto& get(Tuple<Types...>& tuple)
{
	using Subtuple = typename TupleElement<Index, ::Lumix::Tuple<Types...> >::Tail;
	return (((Subtuple&)tuple).value);
}


template <class F, class Tuple, int... I>
constexpr void apply_impl(F& f, Tuple& t, Indices<I...>)
{
	using expand = bool[];
	(void)expand
	{
		(
			f(get<I>(t)),
			true
			)...
	};
}

template <class F, class Tuple>
constexpr void apply_impl(F& f, Tuple& t, Indices<>) {}

template <class F, class Tuple>
constexpr void apply(F& f, Tuple& t)
{
	apply_impl(f, t, typename BuildIndices<-1, TupleSize<Tuple>::result>::result{});
}


template <class F, class Tuple, int... I>
constexpr void apply_impl(const F& f, Tuple& t, Indices<I...>)
{
	using expand = bool[];
	(void)expand
	{
		(
			f(get<I>(t)),
			true
			)...
	};
}

template <class F, class Tuple>
constexpr void apply_impl(const F& f, Tuple& t, Indices<>) {}

template <class F, class Tuple>
constexpr void apply(const F& f, Tuple& t)
{
	apply_impl(f, t, typename BuildIndices<-1, TupleSize<Tuple>::result>::result{});
}


template <typename T> struct ResultOf;
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)> { using Type = R; };
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)const> { using Type = R; };

template <typename T> struct ArgCount;
template <typename R, typename C, typename... Args> struct ArgCount<R(C::*)(Args...)> { static const int result = sizeof...(Args); };
template <typename R, typename C, typename... Args> struct ArgCount<R(C::*)(Args...) const> { static const int result = sizeof...(Args); };

template <typename R, typename C, typename... Args> Tuple<Args...> argsToTuple(R(C::*)(Args...));
template <typename R, typename C, typename... Args> Tuple<Args...> argsToTuple(R(C::*)(Args...) const);
template <typename R, typename... Args> Tuple<Args...> argsToTuple(R(Args...));
template <typename F> decltype(argsToTuple(&F::operator())) argsToTuple(F);

template <int N, typename T> struct ArgNType;
template <int N, typename R, typename C, typename... Args> struct ArgNType<N, R(C::*)(Args...)> { using Type = typename TupleElement<N, Tuple<Args...>>::Head; };
template <int N, typename R, typename C, typename... Args> struct ArgNType<N, R(C::*)(Args...) const> { using Type = typename TupleElement<N, Tuple<Args...>>::Head; };


template <typename T> struct ClassOf;
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)> { using Type = C; };
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)const > { using Type = C; };
template <typename R, typename C> struct ClassOf<R(C::*)> { using Type = C; };


} // namespace Lumix
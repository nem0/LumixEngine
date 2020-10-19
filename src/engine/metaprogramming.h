#pragma once


namespace Lumix
{

template <typename T, typename R> struct IsSame { static constexpr bool Value = false; };
template <typename T> struct IsSame<T, T> { static constexpr bool Value = true; };
template <typename T> struct RemoveReference { using Type = T; };
template <typename T> struct RemoveReference<T&> { using Type = T; };
template <typename T> struct RemoveReference<T&&> { using Type = T; };
template <typename T> struct RemoveConst { using Type = T; };
template <typename T> struct RemoveConst<const T> { using Type = T; };
template <typename T> struct RemoveVolatile { using Type = T; };
template <typename T> struct RemoveVolatile<volatile T> { using Type = T; };
template <typename T> using RemoveCR = typename RemoveConst<typename RemoveReference<T>::Type>::Type;
template <typename T> using RemoveCVR = typename RemoveVolatile<RemoveCR<T>>::Type;
template <typename T> constexpr typename RemoveReference<T>::Type&& Move(T&& t) {
	return static_cast<typename RemoveReference<T>::Type&&>(t);
}

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

template <typename... Types>
auto makeTuple(Types... types)
{
	return Tuple<Types...>(types...);
}

template <typename T> struct TupleSize;

template <typename... Types>
struct TupleSize<const Tuple<Types...> >
{
	enum { result = sizeof...(Types) };
};

template <typename... Types>
struct TupleSize<Tuple<Types...> >
{
	enum { result = sizeof...(Types) };
};

template <int Index, typename Tuple> struct TupleElement;

template <typename HeadType, typename... TailTypes>
struct TupleElement<0, const Tuple<HeadType, TailTypes...> >
{
	using Head = HeadType;
	using Tail = const Tuple<HeadType, TailTypes...>;
};

template <int Index, typename Head, typename... Tail>
struct TupleElement<Index, const Tuple<Head, Tail...> >
	: TupleElement<Index - 1, const Tuple<Tail...> >
{
};

template <typename HeadType, typename... TailTypes>
struct TupleElement<0, Tuple<HeadType, TailTypes...> >
{
	using Head = HeadType;
	using Tail = Tuple<HeadType, TailTypes...>;
};

template <int Index, typename Head, typename... Tail>
struct TupleElement<Index, Tuple<Head, Tail...> >
	: TupleElement<Index - 1, Tuple<Tail...> >
{
};


template <int Index, typename... Types>
constexpr auto& get(const Tuple<Types...>& tuple)
{
	using Subtuple = typename TupleElement<Index, const ::Lumix::Tuple<Types...> >::Tail;
	return (((Subtuple&)tuple).value);
}


template <int Index, typename... Types>
constexpr auto& get(Tuple<Types...>& tuple)
{
	using Subtuple = typename TupleElement<Index, ::Lumix::Tuple<Types...> >::Tail;
	return (((Subtuple&)tuple).value);
}


template <typename F, typename Tuple, int... I>
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

template <typename F, typename Tuple>
constexpr void apply_impl(F& f, Tuple& t, Indices<>) {}

template <typename F, typename Tuple>
constexpr void apply(F& f, Tuple& t)
{
	apply_impl(f, t, typename BuildIndices<-1, TupleSize<Tuple>::result>::result{});
}


template <typename F, typename Tuple, int... I>
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

template <typename F, typename Tuple>
constexpr void apply_impl(const F& f, Tuple& t, Indices<>) {}

template <typename F, typename Tuple>
constexpr void apply(const F& f, Tuple& t)
{
	apply_impl(f, t, typename BuildIndices<-1, TupleSize<Tuple>::result>::result{});
}


template <typename T> struct ResultOf;
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)> { using Type = R; };
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...) const> { using Type = R; };
template <typename R, typename C> struct ResultOf<R(C::*)> { using Type = R; };


template <typename T> struct ClassOf;
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)> { using Type = C; };
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)const > { using Type = C; };
template <typename R, typename C> struct ClassOf<R(C::*)> { using Type = C; };


} // namespace Lumix
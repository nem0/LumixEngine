#pragma once


namespace Lumix
{


template <class T> struct RemoveReference { using type = T; };
template <class T> struct RemoveReference<T&> { using type = T; };
template <class T> struct RemoveReference<T&&> { using type = T; };
template <class T> struct RemoveConst { using type = T; };
template <class T> struct RemoveConst<const T> { using type = T; };
template <class T> struct RemoveVolatile { using type = T; };
template <class T> struct RemoveVolatile<volatile T> { using type = T; };
template <class T> using RemoveCR = typename RemoveConst<typename RemoveReference<T>::type>::type;
template <class T> using RemoveCVR = typename RemoveVolatile<RemoveCR<T>>::type;


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

template <typename... Types> struct tuple;

template <>
struct tuple<>
{
	tuple() {}
};

template <typename Head, typename... Tail>
struct tuple<Head, Tail...> : tuple<Tail...>
{
	Head value;

	tuple() {}
	tuple(Head head, Tail... tail) : tuple<Tail...>(tail...), value(head) {}
	
	tuple(const tuple<Head, Tail...>& rhs) : tuple<Tail...>(rhs) { value = rhs.value; }
};


template <typename... Types>
auto make_tuple(Types... types)
{
	using R = tuple<Types...>;
	return tuple<Types...>(types...);
}

template<class _Tuple> struct tuple_size;

template<class... _Types>
struct tuple_size<const tuple<_Types...> >
{
	enum { result = sizeof...(_Types) };
};


template<int _Index, class _Tuple> struct tuple_element;

template<class _This, class... _Rest>
struct tuple_element<0, const tuple<_This, _Rest...> >
{
	typedef _This type;
	typedef const tuple<_This, _Rest...> _Ttype;
};

template<int _Index, class _This, class... _Rest>
struct tuple_element<_Index, const tuple<_This, _Rest...> >
	: public tuple_element<_Index - 1, const tuple<_Rest...> >
{
};


template<int Index, typename... Types>
constexpr auto get(const tuple<Types...>& tuple)
{
	using _Ttype = typename tuple_element<Index, const ::Lumix::tuple<Types...> >::_Ttype;
	return (((_Ttype&)tuple).value);
}


template <class F, class Tuple, int... I>
constexpr void apply_impl(F& f, Tuple& t, Indices<I...>)
{
	using expand = bool[];
	(void)expand
	{
		(
			f(Lumix::get<I>(t)),
			true
			)...
	};
}

template <class F, class Tuple>
constexpr void apply_impl(F& f, Tuple& t, Indices<>) {}

template <class F, class Tuple>
constexpr void apply(F& f, Tuple& t)
{
	apply_impl(f, t, typename BuildIndices<-1, Lumix::tuple_size<Tuple>::result>::result{});
}


} // namespace Lumix
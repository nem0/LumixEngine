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


template <typename T> struct ResultOf;
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)> { using Type = R; };
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...) const> { using Type = R; };
template <typename R, typename C> struct ResultOf<R(C::*)> { using Type = R; };


template <typename T> struct ClassOf;
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)> { using Type = C; };
template <typename R, typename C, typename... Args> struct ClassOf<R(C::*)(Args...)const > { using Type = C; };
template <typename R, typename C> struct ClassOf<R(C::*)> { using Type = C; };


template <typename T> struct ArgsCount;
template <typename R, typename C, typename... Args> struct ArgsCount<R(C::*)(Args...)> { static constexpr u32 value = sizeof...(Args); };
template <typename R, typename C, typename... Args> struct ArgsCount<R(C::*)(Args...)const > { static constexpr u32 value = sizeof...(Args); };
template <typename R, typename C> struct ArgsCount<R(C::*)> { static constexpr u32 value = 0; };


} // namespace Lumix
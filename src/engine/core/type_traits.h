#pragma once

namespace Lumix
{

template <bool, class T = void> struct myenable_if {};
template <class T> struct myenable_if<true, T> {using type = T;};

template <bool B, class T> using myenable_if_t = typename myenable_if<B, T>::type;

template <class T> struct remove_reference
{ // remove rvalue reference
    typedef T type;
};


template <class T> struct remove_reference<T&>
{ // remove rvalue reference
    typedef T type;
};

template <class T> struct remove_reference<T&&>
{ // remove rvalue reference
    typedef T type;
};

template <class T> using remove_reference_t = typename remove_reference<T>::type;

template <class T> inline T&& myforward(remove_reference_t<T>& _Arg)
{
    return (static_cast<T&&>(_Arg));
}

template <class T> inline remove_reference_t<T>&& mymove(T&& t)
{
    return static_cast<remove_reference_t<T>&&>(t);
}

} // ~namespace Lumix

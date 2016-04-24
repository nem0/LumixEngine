#pragma once

namespace Lumix
{

template <class T> struct RemoveReference
{ // remove rvalue reference
    typedef T type;
};


template <class T> struct RemoveReference<T&>
{ // remove rvalue reference
    typedef T type;
};

template <class T> struct RemoveReference<T&&>
{ // remove rvalue reference
    typedef T type;
};

template <class T> using RemoveReferenceT = typename RemoveReference<T>::type;

template <class T> inline T&& myforward(RemoveReferenceT<T>& t)
{
    return (static_cast<T&&>(t));
}

template <class T> inline RemoveReferenceT<T>&& mymove(T&& t)
{
    return static_cast<RemoveReferenceT<T>&&>(t);
}

} // ~namespace Lumix

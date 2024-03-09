#pragma once
#ifndef _WIN32
	#include <signal.h> // SIGTRAP
#endif

#if !defined(_WIN32) && !defined(__linux__)
	#error Platform not supported
#endif

#ifndef ASSERT
	#ifdef LUMIX_DEBUG
		#ifdef _WIN32
			#define LUMIX_DEBUG_BREAK() __debugbreak()
		#else
			#define LUMIX_DEBUG_BREAK()  raise(SIGTRAP) 
		#endif
		#define ASSERT(x) do { const volatile bool lumix_assert_b____ = !(x); if(lumix_assert_b____) LUMIX_DEBUG_BREAK(); } while (false)
	#else
		#if defined _MSC_VER && !defined __clang__
			#define ASSERT(x) __assume(x)
		#else
			#define ASSERT(x) { false ? (void)(x) : (void)0; }
		#endif
	#endif
#endif

namespace Lumix {

#ifdef MAX_PATH
	#undef MAX_PATH
#endif

enum { MAX_PATH = 260 };

using i8 = char;
using u8 = unsigned char;
using i16 = short;
using u16 = unsigned short;
using i32 = int;
using u32 = unsigned int;
#ifdef _WIN32
	using i64 = long long;
	using u64 = unsigned long long;
#else	
	using i64 = long;
	using u64 = unsigned long;
#endif
using uintptr = u64;

static_assert(sizeof(uintptr) == sizeof(void*), "Incorrect size of uintptr");
static_assert(sizeof(i64) == 8, "Incorrect size of i64");
static_assert(sizeof(i32) == 4, "Incorrect size of i32");
static_assert(sizeof(i16) == 2, "Incorrect size of i16");
static_assert(sizeof(i8) == 1, "Incorrect size of i8");

template <typename T, u32 count> constexpr u32 lengthOf(const T (&)[count])
{
	return count;
};

template<bool, class T> struct EnableIf {};
template <class T> struct EnableIf<true, T> {  using Type = T; };
template <class T> inline constexpr bool is_enum_v = __is_enum(T);
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr T operator | (T a, T b) { return T(u64(a) | u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr T operator & (T a, T b) { return T(u64(a) & u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr T operator ^ (T a, T b) { return T(u64(a) ^ u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr void operator |= (T& a, T b) { a = T(u64(a) | u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr void operator &= (T& a, T b) { a = T(u64(a) & u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr void operator ^= (T& a, T b) { a = T(u64(a) ^ u64(b)); }
template <typename T, typename EnableIf<is_enum_v<T>, int>::Type = 0> constexpr T operator ~ (T a) { return T(~u64(a)); }
template <typename E> bool isFlagSet(E flags, E flag) { return ((u64)flags & (u64)flag); }
template <typename E> void setFlag(E& flags, E flag, bool set) {
	if (set) flags = E((u64)flags | (u64)flag); 
	else flags = E(u64(flags) & ~u64(flag));
}

#ifdef _WIN32
	#define LUMIX_LIBRARY_EXPORT __declspec(dllexport)
	#define LUMIX_LIBRARY_IMPORT __declspec(dllimport)
	#define LUMIX_FORCE_INLINE __forceinline
	#define LUMIX_RESTRICT __restrict
#else 
	#define LUMIX_LIBRARY_EXPORT __attribute__((visibility("default")))
	#define LUMIX_LIBRARY_IMPORT 
	#define LUMIX_FORCE_INLINE __attribute__((always_inline)) inline
	#define LUMIX_RESTRICT __restrict__
#endif

#ifdef STATIC_PLUGINS
	#define LUMIX_CORE_API
#elif defined BUILDING_CORE
	#define LUMIX_CORE_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_CORE_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef _MSC_VER
	#pragma warning(error : 4101)
	#pragma warning(error : 4127)
	#pragma warning(error : 4263)
	#pragma warning(error : 4265)
	#pragma warning(error : 4296)
	#pragma warning(error : 4456)
	#pragma warning(error : 4062)
	#pragma warning(error : 5233)
	#pragma warning(error : 5245)
	#pragma warning(disable : 4251)
	// this is disabled because VS19 16.5.0 has false positives :(
	#pragma warning(disable : 4724)
	#if _MSC_VER == 1900 
		#pragma warning(disable : 4091)
	#endif
#endif

#ifdef __clang__
	#pragma clang diagnostic ignored "-Wreorder-ctor"
	#pragma clang diagnostic ignored "-Wunknown-pragmas"
	#pragma clang diagnostic ignored "-Wignored-pragma-optimize"
	#pragma clang diagnostic ignored "-Wmissing-braces"
	#pragma clang diagnostic ignored "-Wchar-subscripts"
#endif

} // namespace Lumix

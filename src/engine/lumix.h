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

struct EntityRef;

struct EntityPtr
{
	EntityPtr() : index(-1) {}
	explicit EntityPtr(i32 index) : index(index) {}
	i32 index;
	bool operator==(const EntityPtr& rhs) const { return rhs.index == index; }
	bool operator<(const EntityPtr& rhs) const { return rhs.index < index; }
	bool operator>(const EntityPtr& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityPtr& rhs) const { return rhs.index != index; }
	operator bool() const { return index >= 0; }
	bool isValid() const { return index >= 0; }
	inline explicit operator EntityRef() const;
	inline EntityRef operator *() const;
};

struct EntityRef
{
	i32 index;
	bool operator==(const EntityRef& rhs) const { return rhs.index == index; }
	bool operator<(const EntityRef& rhs) const { return rhs.index < index; }
	bool operator>(const EntityRef& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityRef& rhs) const { return rhs.index != index; }
	operator EntityPtr() const { return EntityPtr{index}; }
};

struct ComponentType
{
	enum { MAX_TYPES_COUNT = 64 };

	i32 index;
	bool operator==(const ComponentType& rhs) const { return rhs.index == index; }
	bool operator<(const ComponentType& rhs) const { return rhs.index < index; }
	bool operator>(const ComponentType& rhs) const { return rhs.index > index; }
	bool operator!=(const ComponentType& rhs) const { return rhs.index != index; }
};
const ComponentType INVALID_COMPONENT_TYPE = {-1};
const EntityPtr INVALID_ENTITY = EntityPtr{-1};

template <typename T, u32 count> constexpr u32 lengthOf(const T (&)[count])
{
	return count;
};

template <typename T>
struct Span
{
	Span() : m_begin(nullptr), m_end(nullptr) {}
	Span(T* begin, u32 len) : m_begin(begin), m_end(begin + len) {}
	Span(T* begin, T* end) : m_begin(begin), m_end(end) {}
	template <int N> Span(T (&value)[N]) : m_begin(value), m_end(value + N) {}
	T& operator[](u32 idx) const { ASSERT(m_begin + idx < m_end); return m_begin[idx]; }
	operator Span<const T>() const { return Span<const T>(m_begin, m_end); }
	void removePrefix(u32 count) { ASSERT(count <= length()); m_begin += count; }
	void removeSuffix(u32 count) { ASSERT(count <= length()); m_end -= count; }
	[[nodiscard]] Span fromLeft(u32 count) const { ASSERT(count <= length()); return Span(m_begin + count, m_end); }
	[[nodiscard]] Span fromRight(u32 count) const { ASSERT(count <= length()); return Span(m_begin, m_end - count); }
	T& back() { ASSERT(length() > 0); return *(m_end - 1); }
	const T& back() const { ASSERT(length() > 0); return *(m_end - 1); }
	bool equals(const Span<T>& rhs) {
		bool res = true;
		if (length() != rhs.length()) return false;
		for (const T& v : *this) {
			u32 i = u32(&v - m_begin);
			if (v != rhs.m_begin[i]) return false;
		}
		return true;
	}

	template <typename F>
	i32 find(const F& f) const { 
		for (u32 i = 0, c = length(); i < c; ++i) {
			if (f(m_begin[i])) return i;
		}
		return -1;
	}

	u32 length() const { return u32(m_end - m_begin); }

	T* begin() const { return m_begin; }
	T* end() const { return m_end; }

	T* m_begin;
	T* m_end;
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

#pragma pack(1)
struct Color {
	Color() {}
	Color(u32 abgr) { 
		r = u8(abgr & 0xff);
		g = u8((abgr >> 8) & 0xff);
		b = u8((abgr >> 16) & 0xff);
		a = u8((abgr >> 24) & 0xff);
	}

	Color(u8 r, u8 g, u8 b, u8 a) : r(r), g(g), b(b), a(a) {}

	u32 abgr() const { return ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r; }

	u8 r;
	u8 g;
	u8 b;
	u8 a;

	enum {
		RED = 0xff0000ff,
		GREEN = 0xff00ff00,
		BLUE = 0xffff0000,
		BLACK = 0xff000000,
		WHITE = 0xffFFffFF
	};
};
#pragma pack()

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
	#define LUMIX_ENGINE_API
#elif defined BUILDING_ENGINE
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define LUMIX_EDITOR_API
#elif defined BUILDING_EDITOR
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef STATIC_PLUGINS
	#define LUMIX_RENDERER_API
#elif defined BUILDING_RENDERER
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_IMPORT
#endif

inline EntityPtr::operator EntityRef() const
{
	ASSERT(isValid());
	return {index};
}

inline EntityRef EntityPtr::operator *() const
{
	ASSERT(isValid());
	return {index};
}

namespace reflection { LUMIX_ENGINE_API ComponentType getComponentType(const char* id); }
namespace os { void abort(); }

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

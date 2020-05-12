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
		#ifdef _WIN32		
			#define ASSERT(x) __assume(x)
		#else
			#define ASSERT(x) { false ? (void)(x) : (void)0; }
		#endif
	#endif
#endif

namespace Lumix
{


typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;
typedef u64 uintptr;

static_assert(sizeof(uintptr) == sizeof(void*), "Incorrect size of uintptr");
static_assert(sizeof(i64) == 8, "Incorrect size of i64");
static_assert(sizeof(i32) == 4, "Incorrect size of i32");
static_assert(sizeof(i16) == 2, "Incorrect size of i16");
static_assert(sizeof(i8) == 1, "Incorrect size of i8");

const u32 MAX_PATH_LENGTH = 260;

struct EntityRef;

struct EntityPtr
{
	EntityPtr() : index(-1) {}
	EntityPtr(i32 index) : index(index) {}
	i32 index;
	bool operator==(const EntityPtr& rhs) const { return rhs.index == index; }
	bool operator<(const EntityPtr& rhs) const { return rhs.index < index; }
	bool operator>(const EntityPtr& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityPtr& rhs) const { return rhs.index != index; }
	bool isValid() const { return index >= 0; }
	inline explicit operator EntityRef() const;
};

struct EntityRef
{
	int index;
	bool operator==(const EntityRef& rhs) const { return rhs.index == index; }
	bool operator<(const EntityRef& rhs) const { return rhs.index < index; }
	bool operator>(const EntityRef& rhs) const { return rhs.index > index; }
	bool operator!=(const EntityRef& rhs) const { return rhs.index != index; }
	operator EntityPtr() const { return {index}; }
};

struct ComponentType
{
	enum { MAX_TYPES_COUNT = 64 };

	int index;
	bool operator==(const ComponentType& rhs) const { return rhs.index == index; }
	bool operator<(const ComponentType& rhs) const { return rhs.index < index; }
	bool operator>(const ComponentType& rhs) const { return rhs.index > index; }
	bool operator!=(const ComponentType& rhs) const { return rhs.index != index; }
};
const ComponentType INVALID_COMPONENT_TYPE = {-1};
const EntityPtr INVALID_ENTITY = {-1};

template <typename T, u32 count> constexpr u32 lengthOf(const T (&)[count])
{
	return count;
};

// use this instead non-const reference parameter to show intention
template <typename T>
struct Ref {
	Ref(const Ref<T>& value) : value(value.value) {}
	explicit Ref(T& value) : value(value) {}
	operator T&() { return value; }
	T* operator->() { return &value; } 
	void operator =(const Ref<T>& rhs) { value = rhs.value; }
	void operator =(const T& rhs) { value = rhs; }
	template <typename T2> void operator =(const T2& rhs) { value = rhs; }
	T& value;
};

template <typename T>
struct Span
{
	Span() : m_begin(nullptr), m_end(nullptr) {}
	Span(T* begin, u32 len) : m_begin(begin), m_end(begin + len) {}
	Span(T* begin, T* end) : m_begin(begin), m_end(end) {}
	template <int N> explicit Span(T (&value)[N]) : m_begin(value), m_end(m_begin + N) {}
	T& operator[](u32 idx) const { ASSERT(m_begin + idx < m_end); return m_begin[idx]; }
	operator Span<const T>() const { return Span<const T>(m_begin, m_end); }
	Span fromLeft(u32 count) const { return Span(m_begin + count, m_end); }
	
	u32 length() const { return u32(m_end - m_begin); }

	T* begin() const { return m_begin; }
	T* end() const { return m_end; }

	T* m_begin;
	T* m_end;
};

#pragma pack(1)
struct Color {
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

	static constexpr u32 RED = 0xff0000ff;
	static constexpr u32 GREEN = 0xff00ff00;
	static constexpr u32 BLUE = 0xffff0000;
	static constexpr u32 BLACK = 0xff000000;
	static constexpr u32 WHITE = 0xffFFffFF;
};

#pragma pack()

struct Time {
	Time() {}
	static Time fromSeconds(float time) {
		ASSERT(time >= 0);
		return { u32(time * ONE_SECOND) };
	}
	float seconds() const { return float(value / double(ONE_SECOND)); }
	Time operator+(const Time& rhs) const { return { value + rhs.value }; } 
	void operator+=(const Time& rhs) { value += rhs.value; }
	bool operator<(const Time& rhs) const { return value < rhs.value; }
	bool operator<=(const Time& rhs) const { return value <= rhs.value; }
	Time operator%(const Time& rhs) const { return {value % rhs.value }; }
	u32 raw() const { return value; }

private:
	Time(u32 v) : value(v) {}
	u32 value;
	static constexpr u32 ONE_SECOND = 1 << 15;
};

#ifdef _WIN32
	#define LUMIX_LIBRARY_EXPORT __declspec(dllexport)
	#define LUMIX_LIBRARY_IMPORT __declspec(dllimport)
	#define LUMIX_FORCE_INLINE __forceinline
	#define LUMIX_RESTRICT __restrict
	#define LUMIX_ATTRIBUTE_USED
#else 
	#define LUMIX_LIBRARY_EXPORT __attribute__((visibility("default")))
	#define LUMIX_LIBRARY_IMPORT 
	#define LUMIX_FORCE_INLINE __attribute__((always_inline)) inline
	#define LUMIX_RESTRICT __restrict__
	#define LUMIX_ATTRIBUTE_USED __attribute__((used))
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

#ifdef _MSC_VER
	#pragma warning(disable : 4251)
	// this is disabled because VS19 16.5.0 has false positives :(
	#pragma warning(disable : 4724)
	#if _MSC_VER == 1900 
		#pragma warning(disable : 4091)
	#endif
#endif

} // namespace Lumix

#pragma once

#define LUMIX_SINGLE_THREAD() 0

#ifndef _WIN32
	#include <signal.h> // SIGTRAP
#endif

#ifdef _WIN32
	#ifdef _WIN64
		#define PLATFORM64
	#else
		#define PLATFORM32
	#endif
#elif defined(__linux__)
	#ifdef __x86_64__
		#define PLATFORM64
	#else
		#define PLATFORM32
	#endif
#elif defined(__EMSCRIPTEN__)
	#define PLATFORM32
#else
	#error Platform not supported
#endif

#define STRINGIZE_2( _ ) #_
#define STRINGIZE( _ ) STRINGIZE_2( _ )


namespace Lumix
{


typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;

#ifdef _WIN32
	typedef long long int64;
	typedef unsigned long long uint64;
	#define LUMIX_ALIGN_BEGIN(_align) __declspec(align(_align))
	#define LUMIX_ALIGN_END(_align)
#elif defined __EMSCRIPTEN__
	typedef long long int64;
	typedef unsigned long long uint64;
	#define LUMIX_ALIGN_BEGIN(_align)
	#define LUMIX_ALIGN_END(_align) __attribute__( (aligned(_align) ) )
#else
	#ifdef PLATFORM64
		typedef long int64;
		typedef unsigned long uint64;
	#else
		typedef long long int64;
		typedef unsigned long long uint64;
	#endif
	#define LUMIX_ALIGN_BEGIN(_align)
	#define LUMIX_ALIGN_END(_align) __attribute__( (aligned(_align) ) )
#endif

#ifdef PLATFORM64
	typedef uint64 uintptr;
#else
	typedef uint32 uintptr;
#endif

static_assert(sizeof(int64) == 8, "Incorrect size of int64");
static_assert(sizeof(int32) == 4, "Incorrect size of int32");
static_assert(sizeof(int16) == 2, "Incorrect size of int16");
static_assert(sizeof(int8) == 1, "Incorrect size of int8");

const uint32 MAX_PATH_LENGTH = 260;
struct ComponentHandle
{
	int index;
	bool operator==(const ComponentHandle& rhs) const { return rhs.index == index; };
	bool operator<(const ComponentHandle& rhs) const { return rhs.index < index; };
	bool operator>(const ComponentHandle& rhs) const { return rhs.index > index; };
	bool operator!=(const ComponentHandle& rhs) const { return rhs.index != index; };
};
inline bool isValid(ComponentHandle cmp) { return cmp.index >= 0; }

struct Entity
{
	int index;
	bool operator==(const Entity& rhs) const { return rhs.index == index; };
	bool operator<(const Entity& rhs) const { return rhs.index < index; };
	bool operator>(const Entity& rhs) const { return rhs.index > index; };
	bool operator!=(const Entity& rhs) const { return rhs.index != index; };
};
inline bool isValid(Entity entity) { return entity.index >= 0; }
struct ComponentType
{
	int index;
	bool operator==(const ComponentType& rhs) const { return rhs.index == index; };
	bool operator<(const ComponentType& rhs) const { return rhs.index < index; };
	bool operator>(const ComponentType& rhs) const { return rhs.index > index; };
	bool operator!=(const ComponentType& rhs) const { return rhs.index != index; };
};
const ComponentType INVALID_COMPONENT_TYPE = {-1};
const Entity INVALID_ENTITY = {-1};
const ComponentHandle INVALID_COMPONENT = {-1};

template <typename T, int count> int lengthOf(const T (&)[count])
{
	return count;
};


} // namespace Lumix


#ifndef ASSERT
	#ifdef NDEBUG
		#define ASSERT(x) { false ? (void)(x) : 0; } 
	#else
		#ifdef _WIN32
			#define LUMIX_DEBUG_BREAK() __debugbreak()
		#else
			#define LUMIX_DEBUG_BREAK()  raise(SIGTRAP) 
		#endif
		#define ASSERT(x) do { const volatile bool lumix_assert_b____ = !(x); if(lumix_assert_b____) LUMIX_DEBUG_BREAK(); } while (false)
	#endif
#endif

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

#ifdef _MSC_VER
	#pragma warning(disable : 4251)
	#pragma warning(disable : 4365)
	#pragma warning(disable : 4512)
	#pragma warning(disable : 4996)
	#if _MSC_VER == 1900 
		#pragma warning(disable : 4091)
	#endif
#endif
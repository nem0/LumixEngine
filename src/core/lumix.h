#pragma once

// usefull compile time messages

#define STRINGIZE_2( _ ) #_
#define STRINGIZE( _ ) STRINGIZE_2( _ )

#define JOIN_STRINGS_2(A, B) A ## B
#define JOIN_STRINGS(A, B) JOIN_STRINGS_2(A, B)

#define LUMIX_FORCE_SYMBOL(symbol) \
	__pragma(comment(linker, "/INCLUDE:_" STRINGIZE(symbol)))

#define NOTE(msg)\
	__pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : NOTE: " msg))

#define TODO(msg)\
	__pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : TODO: " msg))

#define WARN(msg)\
	__pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : WARNING: " msg))


#include <type_traits>	
#include <cstdint>
#include <cassert>


namespace Lumix
{
	const uint32_t MAX_PATH_LENGTH = 260;
	typedef int ComponentIndex;
	typedef int Entity;
	const int INVALID_ENTITY = -1;
	const int INVALID_COMPONENT = -1;
}


template <typename T, int count>
int lengthOf(const T(&)[count])
{
	return count;
};


#ifndef ASSERT
	#ifdef _WIN32
		#ifdef NDEBUG
			#define ASSERT(x) { false ? (void)(x) : 0; } 
		#else
			#define ASSERT(x) { const volatile bool lumix_assert_b____ = !(x); if(lumix_assert_b____) __debugbreak(); } 
		#endif
	#else
		#define ASSERT(x) assert(x)
	#endif
#endif

#define LUMIX_LIBRARY_EXPORT __declspec(dllexport)
#define LUMIX_LIBRARY_IMPORT __declspec(dllimport)
#define LUMIX_ALIGN_OF(T) __alignof(T)
#define LUMIX_FORCE_INLINE __forceinline


#ifdef BUILDING_PHYSICS
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_ENGINE
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_SCRIPT
	#define LUMIX_SCRIPT_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_SCRIPT_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_ANIMATION
	#define LUMIX_ANIMATION_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ANIMATION_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_EDITOR
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_IMPORT
#endif


#define LUMIX_RESTRICT __restrict

#pragma warning(disable : 4251)
#pragma warning(disable : 4512)
#pragma warning(disable : 4996)

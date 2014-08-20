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
#include <malloc.h>

const uint32_t LUMIX_MAX_PATH = 260;

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

#ifndef NULL
#define NULL nullptr
#endif

#define LUMIX_FORCE_INLINE __forceinline

#ifdef BUILDING_CORE
#define LUMIX_CORE_API __declspec(dllexport)
#else
#define LUMIX_CORE_API __declspec(dllimport)
#endif


#ifdef BUILDING_PHYSICS
#define LUMIX_PHYSICS_API __declspec(dllexport)
#else
#define LUMIX_PHYSICS_API __declspec(dllimport)
#endif


#ifdef BUILDING_NAVIGATION
#define LUMIX_NAVIGATION_API __declspec(dllexport)
#else
#define LUMIX_NAVIGATION_API __declspec(dllimport)
#endif


#ifdef BUILDING_ENGINE
#define LUMIX_ENGINE_API __declspec(dllexport)
#else
#define LUMIX_ENGINE_API __declspec(dllimport)
#endif

#ifdef BUILDING_SCRIPT
#define LUMIX_SCRIPT_API __declspec(dllexport)
#else
#define LUMIX_SCRIPT_API __declspec(dllimport)
#endif

#define LUMIX_RESTRICT __restrict

#include "core/new.h"
#include "core/new_macros.h"

#pragma warning(disable : 4251)
#pragma warning(disable : 4996)

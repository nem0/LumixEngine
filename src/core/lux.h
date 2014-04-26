#pragma once

// usefull compile time messages

#define STRINGIZE_2( _ ) #_
#define STRINGIZE( _ ) STRINGIZE_2( _ )

#define JOIN_STRINGS_2(A, B) A ## B
#define JOIN_STRINGS(A, B) JOIN_STRINGS_2(A, B)

#define LUX_FORCE_SYMBOL(symbol) \
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

const uint32_t LUX_MAX_PATH = 260;

#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

#ifndef STATIC_ASSERT
#define STATIC_ASSERT static_assert
#endif

#ifndef NULL
#define NULL nullptr
#endif

#define LUX_INLINE __inline
#define LUX_FORCE_INLINE __forceinline

#ifdef BUILDING_CORE
#define LUX_CORE_API __declspec(dllexport)
#else
#define LUX_CORE_API __declspec(dllimport)
#endif


#ifdef BUILDING_PHYSICS
#define LUX_PHYSICS_API __declspec(dllexport)
#else
#define LUX_PHYSICS_API __declspec(dllimport)
#endif


#ifdef BUILDING_NAVIGATION
#define LUX_NAVIGATION_API __declspec(dllexport)
#else
#define LUX_NAVIGATION_API __declspec(dllimport)
#endif


#ifdef BUILDING_ENGINE
#define LUX_ENGINE_API __declspec(dllexport)
#else
#define LUX_ENGINE_API __declspec(dllimport)
#endif

#ifdef BUILDING_GUI
#define LUX_GUI_API __declspec(dllexport)
#else
#define LUX_GUI_API __declspec(dllimport)
#endif

#include "core/new.h"
#include "core/new_macros.h"

#pragma warning(disable : 4251)
#pragma warning(disable : 4996)

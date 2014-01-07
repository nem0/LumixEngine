#pragma once


#include <cstdint>
#include <cassert>
#include <malloc.h>

#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

#ifndef NULL
#define NULL 0
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


#define LUX_FINAL final
#define LUX_OVERRIDE override
#define LUX_ABSTRACT abstract

#define LUX_NEW(T) new T
#define LUX_NEW_ARRAY(T, count) new T[count]
#define LUX_DELETE(ptr) delete (ptr);
#define LUX_DELETE_ARRAY(ptr) delete[] (ptr)
#define LUX_MALLOC(size) malloc(size)
#define LUX_FREE(ptr) free(ptr)
#define LUX_REALLOC(ptr, size) realloc(ptr, size)
#define LUX_MALLOC_ALIGNED(size, alignment) _aligned_malloc(size, alignment)
#define LUX_FREE_ALIGNED(ptr) _aligned_free(ptr)

#pragma warning(disable : 4251)
#pragma warning(disable : 4996)

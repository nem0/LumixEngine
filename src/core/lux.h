#pragma once


#include <cstdint>

#ifndef NULL
#define NULL 0
#endif


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

#ifdef BUILDING_PLATFORM
#define LUX_PLATFORM_API __declspec(dllexport)
#else
#define LUX_PLATFORM_API __declspec(dllimport)
#endif

#define LUX_FINAL final
#define LUX_OVERRIDE override
#define LUX_ABSTRACT abstract

#pragma warning( disable : 4251 )
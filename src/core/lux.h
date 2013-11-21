#pragma once


#ifdef BUILDING_CORE
#define LUX_CORE_API __declspec(dllexport)
#else
#define LUX_CORE_API __declspec(dllimport)
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

#define DISABLE_PHYSICS

#pragma warning( disable : 4251 )
/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_FOUNDATION_PX_PREPROCESSOR_H
#define PX_FOUNDATION_PX_PREPROCESSOR_H

#include <stddef.h>

/** \addtogroup foundation
  @{
*/


/**
List of preprocessor defines used to configure the SDK
- PX_DEBUG: enable asserts (exactly one needs to be defined)
- PX_CHECKED: enable run time checks, mostly unused or equiv. to PX_DEBUG
- PX_SUPPORT_VISUAL_DEBUGGER: ...
- AG_PERFMON: ... (Deprecated)
*/

/**
Compiler define
*/
#ifdef _MSC_VER 
#	define PX_VC
#	if _MSC_VER >= 1900
#		define PX_VC14
#	elif _MSC_VER >= 1800
#		define PX_VC12
#   elif _MSC_VER >= 1700
#       define PX_VC11
#   elif _MSC_VER >= 1600
#       define PX_VC10
#   elif _MSC_VER >= 1500
#       define PX_VC9
#	elif _MSC_VER >= 1400
#		define PX_VC8
#	elif _MSC_VER >= 1300
#		define PX_VC7
#	else
#		define PX_VC6
#	endif
#elif defined(__ghs__)
#   define PX_GHS
#elif __GNUC__ || __SNC__
#	define PX_GNUC
#else
#	error "Unknown compiler"
#endif

/**
Platform define
*/
#ifdef PX_VC
#	ifdef XBOXONE
#		define PX_XBOXONE
#		define PX_X64
#	elif defined(_M_IX86)
#		define PX_X86
#		define PX_WINDOWS
#   elif defined(_M_X64)
#       define PX_X64
#       define PX_WINDOWS
#	elif defined(_M_PPC)
#		define PX_PPC
#		define PX_X360
#		define PX_VMX
#   elif defined(_M_ARM)
#		define PX_ARM
#       define PX_ARM_NEON
#	else
#		error "Unknown platform"
#	endif
#	if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_PARTITION_APP)
#		define PX_WINMODERN
#	endif
#elif defined PX_GNUC
#   ifdef __CELLOS_LV2__
#	define PX_PS3
#		define PX_VMX
#   elif defined(__arm__)
#		define PX_ARM
#		if defined(__SNC__)
#			define PX_PSP2
#		endif
#		if defined(__ARM_NEON__)
#			define PX_ARM_NEON
#		endif
#   elif defined(__arm64__)
#		define PX_A64
#		if defined(__ARM_NEON__)
#			define PX_ARM_NEON
#		endif
#   elif defined(__i386__)
#       define PX_X86
#		define PX_VMX
#   elif defined(__x86_64__)
#       ifdef __ORBIS__
#         define PX_PS4
#		  define PX_X64
#       else
#       define PX_X64
#       endif
#   elif defined(__ppc__)
#       define PX_PPC
#   elif defined(__ppc64__)
#       define PX_PPC
#	define PX_PPC64
#   else
#	error "Unknown platform"
#   endif
#	if defined(ANDROID)
#   	define PX_ANDROID
#   	define PX_UNIX
#	elif defined(__linux__)
#   	define PX_LINUX
#   	define PX_UNIX
#	elif defined(__APPLE__)
#   	define PX_APPLE
#   	define PX_UNIX
#		if defined(__arm__) || defined(__arm64__)
#			define PX_APPLE_IOS
#		else
#			define PX_OSX
#		endif
#	elif defined(__CYGWIN__)
#   	define PX_CYGWIN
#   	define PX_LINUX
#   	define PX_UNIX
#	endif
#elif defined PX_GHS
#	define PX_WIIU
#endif

#if defined(PX_X64) || defined(PX_A64)
#define PX_P64 // pointers are 64 bit
#endif

/**
DLL export macros
*/
#if !defined(PX_C_EXPORT) 
#	if defined(PX_WINDOWS) || defined(PX_WINMODERN) || defined(PX_LINUX)
#		define PX_C_EXPORT extern "C"
#	else
#		define PX_C_EXPORT
#	endif
#endif

#if (defined(PX_UNIX) && (__GNUC__ >= 4))
#	define PX_UNIX_EXPORT __attribute__ ((visibility ("default")))
#else
#	define PX_UNIX_EXPORT
#endif

/**
Define API function declaration

PX_FOUNDATION_EXPORTS - used by the DLL library (PhysXCommon) to export the API
PX_FOUNDATION_NO_EXPORTS - exists because there are windows configurations where 
						   the PX_FOUNDATION_API is linked through standard static linking
no definition - this will allow DLLs and libraries to use the exported API from PhysXCommon				   

*/
#if (defined(PX_WINDOWS) || defined(PX_WINMODERN) ) && !defined(__CUDACC__)
	#if defined PX_FOUNDATION_EXPORTS
		#define PX_FOUNDATION_API __declspec(dllexport)
	#elif defined PX_FOUNDATION_NO_EXPORTS
		#define PX_FOUNDATION_API
	#else
		#define PX_FOUNDATION_API __declspec(dllimport)
	#endif
#elif defined(PX_UNIX)
	#define PX_FOUNDATION_API PX_UNIX_EXPORT
#else
	#define PX_FOUNDATION_API
#endif

/**
Calling convention
*/
#ifndef PX_CALL_CONV
#	if defined PX_WINDOWS
#		define PX_CALL_CONV __cdecl
#	else
#		define PX_CALL_CONV
#	endif
#endif

/**
Pack macros - disabled on SPU because they are not supported
*/
#if defined(PX_VC)
#	define PX_PUSH_PACK_DEFAULT	__pragma( pack(push, 8) )
#	define PX_POP_PACK			__pragma( pack(pop) )
#elif (defined(PX_GNUC) && !defined(__SPU__)) || defined(PX_GHS)
#	define PX_PUSH_PACK_DEFAULT	_Pragma("pack(push, 8)")
#	define PX_POP_PACK			_Pragma("pack(pop)")
#else
#	define PX_PUSH_PACK_DEFAULT
#	define PX_POP_PACK
#endif

/**
Inline macro
*/
#if defined(PX_WINDOWS) || defined(PX_X360) || defined(PX_WINMODERN) || defined(PX_XBOXONE)
#	define PX_INLINE inline
#	pragma inline_depth( 255 )
#else
#	define PX_INLINE inline
#endif

/**
Force inline macro
*/
#if defined(PX_VC)
	#define PX_FORCE_INLINE __forceinline
#elif defined(PX_LINUX) // Workaround; Fedora Core 3 do not agree with force inline and PxcPool
	#define PX_FORCE_INLINE inline
#elif defined(PX_GNUC) || defined(PX_GHS)
	#define PX_FORCE_INLINE inline __attribute__((always_inline))
#else
	#define PX_FORCE_INLINE inline
#endif

/**
Noinline macro
*/
#if defined PX_WINDOWS || defined PX_XBOXONE
#	define PX_NOINLINE __declspec(noinline)
#elif defined(PX_GNUC) || defined(PX_GHS)
#	define PX_NOINLINE __attribute__ ((noinline))
#else
#	define PX_NOINLINE 
#endif

/*! restrict macro */
#if defined(__CUDACC__)
#	define PX_RESTRICT __restrict__
#else
//PX_GNUC, PX_VC and PX_GHS all support restrict.  If adding support for a compiler which does not like restrict, please add an exception for it by in that case defining PX_RESTRICT to naught.
#	define PX_RESTRICT __restrict
#endif

#if defined(PX_WINDOWS) || defined(PX_X360) || defined(PX_WINMODERN) || defined(PX_XBOXONE)
#define PX_NOALIAS __declspec(noalias)
#else
#define PX_NOALIAS
#endif


/**
Alignment macros

PX_ALIGN_PREFIX and PX_ALIGN_SUFFIX can be used for type alignment instead of aligning individual variables as follows:
PX_ALIGN_PREFIX(16)
struct A {
...
} PX_ALIGN_SUFFIX(16);
This declaration style is parsed correctly by Visual Assist.

*/
#ifndef PX_ALIGN
	#if defined(PX_VC)
		#define PX_ALIGN(alignment, decl) __declspec(align(alignment)) decl
		#define PX_ALIGN_PREFIX(alignment) __declspec(align(alignment))
		#define PX_ALIGN_SUFFIX(alignment)
	#elif defined(PX_GNUC) || defined(PX_GHS) || defined(PX_APPLE_IOS)
		#define PX_ALIGN(alignment, decl) decl __attribute__ ((aligned(alignment)))
		#define PX_ALIGN_PREFIX(alignment)
		#define PX_ALIGN_SUFFIX(alignment) __attribute__ ((aligned(alignment)))
	#else
		#define PX_ALIGN(alignment, decl)
		#define PX_ALIGN_PREFIX(alignment)
		#define PX_ALIGN_SUFFIX(alignment)
	#endif
#endif

/**
Deprecated macro
- To deprecate a function: Place PX_DEPRECATED at the start of the function header (leftmost word).
- To deprecate a 'typedef', a 'struct' or a 'class': Place PX_DEPRECATED directly after the keywords ('typdef', 'struct', 'class').
*/
#if 0 // set to 1 to create warnings for deprecated functions
#	define PX_DEPRECATED __declspec(deprecated)
#else 
#	define PX_DEPRECATED
#endif

// VC6 no '__FUNCTION__' workaround
#if defined PX_VC6 && !defined __FUNCTION__
#	define __FUNCTION__	"Undefined"
#endif

/**
General defines
*/

// static assert
#if defined(__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)))  
#define PX_COMPILE_TIME_ASSERT(exp)	typedef char PxCompileTimeAssert_Dummy[(exp) ? 1 : -1] __attribute__((unused))
#else 
#define PX_COMPILE_TIME_ASSERT(exp)	typedef char PxCompileTimeAssert_Dummy[(exp) ? 1 : -1]
#endif

#if defined(PX_GNUC)
#define PX_OFFSET_OF(X, Y) __builtin_offsetof(X, Y)
#else
#define PX_OFFSET_OF(X, Y) offsetof(X, Y)
#endif

#define PX_SIZE_OF(Class, Member)			sizeof(((Class*)0)->Member)

// check that exactly one of NDEBUG and _DEBUG is defined
#if !(defined NDEBUG ^ defined _DEBUG)
	#error Exactly one of NDEBUG and _DEBUG needs to be defined by preprocessor
#endif

// make sure PX_CHECKED is defined in all _DEBUG configurations as well
#if !defined(PX_CHECKED) && defined _DEBUG
#define PX_CHECKED
#endif

#ifdef __CUDACC__
#define PX_CUDA_CALLABLE __host__ __device__
#else
#define PX_CUDA_CALLABLE
#endif

// Support GPU PhysX
#if (defined(PX_WINDOWS) && !defined(PX_WINMODERN) && !defined(PX_VC14)) || defined(PX_LINUX)
#define PX_SUPPORT_GPU_PHYSX 1
#else
#define PX_SUPPORT_GPU_PHYSX 0
#endif

// avoid unreferenced parameter warning (why not just disable it?)
// PT: or why not just omit the parameter's name from the declaration????
template <class T> PX_CUDA_CALLABLE PX_INLINE void PX_UNUSED(T const&) {}

	// Ensure that the application hasn't tweaked the pack value to less than 8, which would break
	// matching between the API headers and the binaries
	// This assert works on win32/win64/360/ps3, but may need further specialization on other platforms.
	// Some GCC compilers need the compiler flag -malign-double to be set.
	// Apparently the apple-clang-llvm compiler doesn't support malign-double.

struct PxPackValidation { char _; long long a; };

#if !defined(PX_APPLE)
PX_COMPILE_TIME_ASSERT(PX_OFFSET_OF(PxPackValidation, a) == 8);
#endif

// use in a cpp file to suppress LNK4221
#if defined(PX_VC)
#define PX_DUMMY_SYMBOL namespace { char PxDummySymbol; } 
#else
#define PX_DUMMY_SYMBOL
#endif

#ifdef __SPU__
#define PX_IS_SPU 1
#else
#define PX_IS_SPU 0
#endif

#ifdef PX_X64
#define PX_IS_X64 1
#else
#define PX_IS_X64 0
#endif

#ifdef PX_WINDOWS
#define PX_IS_WINDOWS 1
#else
#define PX_IS_WINDOWS 0
#endif

#ifdef PX_X86
#define PX_IS_X86 1
#else
#define PX_IS_X86 0
#endif

#ifdef PX_X64
#define PX_IS_X64 1
#else
#define PX_IS_X64 0
#endif

#if defined(PX_X86) || defined(PX_X64)
#define PX_IS_INTEL 1
#else
#define PX_IS_INTEL 0
#endif

#ifdef PX_X360
#define PX_IS_X360 1
#else
#define PX_IS_X360 0
#endif

#ifdef PX_PS3
#define PX_IS_PS3 1
#else
#define PX_IS_PS3 0
#endif

#ifdef PX_NVTX
#define PX_NVTX 1
#else
#define PX_NVTX 0
#endif

#define PX_IS_PPU (PX_IS_PS3 && !PX_IS_SPU) // PS3 PPU

#ifdef PX_GNUC
#define PX_WEAK_SYMBOL __attribute__((weak)) // this is to support SIMD constant merging in template specialization
#else
#define PX_WEAK_SYMBOL
#endif

/** @} */
#endif // PX_FOUNDATION_PX_PREPROCESSOR_H

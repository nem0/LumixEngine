// This code contains NVIDIA Confidential Information and is disclosed to you 
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and 
// any modifications thereto. Any use, reproduction, disclosure, or 
// distribution of this software and related documentation without an express 
// license agreement from NVIDIA Corporation is strictly prohibited.
// 
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2012 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_FOUNDATION_PX_ASSERT_H
#define PX_FOUNDATION_PX_ASSERT_H

/** \addtogroup foundation 
@{ */

#include "foundation/Px.h"

#ifdef PX_WINDOWS

#include <stdio.h>
#include <crtdbg.h>

extern void __debugbreak();
#ifndef PX_DOXYGEN

namespace physx
{
#endif
		PX_INLINE void PxAssert(const char* exp, const char* file, int line, bool* ignore)
		{
			//printf("Assertion failed: %s, file %s, line %d\n", exp, file, line);
#ifdef _DEBUG
			if (ignore != NULL)
			{
				int reportType = _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE);
				// PH: _CrtDbgReport returns -1 on error, 0 on 'ignore', 1 on 'retry'. Hitting 'abort' will terminate the process immediately.
				// If the mode is not 'Window', we just always break.
				*ignore = *ignore || ((reportType == _CRTDBG_MODE_WNDW) && (_CrtDbgReport(_CRT_ASSERT, file, line, NULL, "%s", exp) == 0));
			}

			if(ignore == NULL || !*ignore)
#else
			PX_FORCE_PARAMETER_REFERENCE(exp);
			PX_FORCE_PARAMETER_REFERENCE(file);
			PX_FORCE_PARAMETER_REFERENCE(line);
			PX_FORCE_PARAMETER_REFERENCE(ignore);
#endif
				__debugbreak();
		}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#ifdef __CUDACC__
#	define PX_ASSERT(exp)					((void)0)
#	define PX_ALWAYS_ASSERT_MESSAGE(exp)	((void)0)
#else
#ifndef NDEBUG
#	define PX_ASSERT(exp)					{ static bool ignore = false; (void)( (!!(exp)) || (physx::PxAssert(#exp, __FILE__, __LINE__, &ignore), false) ); }
#	define PX_ALWAYS_ASSERT_MESSAGE(exp)	{ static bool ignore = false; (void)( (physx::PxAssert(#exp, __FILE__, __LINE__, &ignore), false) ); }
#else
#	define PX_ASSERT(exp)					((void)0)
#	define PX_ALWAYS_ASSERT_MESSAGE(exp)	((void)0)
#endif
#endif

#elif defined(PX_PS3)
#include "foundation/ps3/PxPS3Assert.h"
#elif defined(PX_ANDROID)
#include <assert.h>
#ifndef NDEBUG
#	include <android/log.h>
#	define PX_ASSERT(exp)					{ if(!(exp)) __android_log_print(ANDROID_LOG_INFO, "PX_ASSERT", "%s: %s:%d\n", #exp, __FILE__, __LINE__); assert(exp); }
#	define PX_ALWAYS_ASSERT_MESSAGE(exp)	{ __android_log_print(ANDROID_LOG_INFO, "PX_ASSERT", "%s: %s:%d\n", #exp, __FILE__, __LINE__); assert((exp) && false); }
#else
#	define PX_ASSERT(exp)					((void)0)
#	define PX_ALWAYS_ASSERT_MESSAGE(exp)	((void)0)
#endif
#else // PX_WINDOWS, other platforms just forward to standard assert
#include <assert.h>
#define PX_ASSERT(exp)						assert(exp)
#define PX_ALWAYS_ASSERT_MESSAGE(exp)		assert((exp) && false)
#endif


#define PX_ALWAYS_ASSERT() PX_ASSERT(0)

 /** @} */
#endif // PX_FOUNDATION_PX_ASSERT_H

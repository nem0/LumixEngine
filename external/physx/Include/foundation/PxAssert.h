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
// Copyright (c) 2008-2014 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_FOUNDATION_PX_ASSERT_H
#define PX_FOUNDATION_PX_ASSERT_H

/** \addtogroup foundation 
@{ */

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	/* Base class to handle assert failures */
	class PxAssertHandler
	{
	public:
		virtual ~PxAssertHandler() {}
		virtual void operator()(const char* exp, const char* file, int line, bool& ignore) = 0;
	};

	PX_FOUNDATION_API PxAssertHandler& PxGetAssertHandler();
	PX_FOUNDATION_API void PxSetAssertHandler(PxAssertHandler& handler);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#if (!defined(PX_DEBUG) && !(defined(PX_CHECKED) && defined(PX_ENABLE_CHECKED_ASSERTS))) || defined(__CUDACC__)
#	define PX_ASSERT(exp) ((void)0)
#	define PX_ALWAYS_ASSERT_MESSAGE(exp) ((void)0)
#   define PX_ASSERT_WITH_MESSAGE(condition, message) ((void)0)
#elif !defined(__SPU__)
	#if defined(_MSC_VER) &&  (_MSC_VER >= 1400)
		// We want to run code analysis on VS2005 and later. This macro will be used to get rid of analysis warning messages if a PX_ASSERT is used to "guard" illegal mem access, for example.
		#define PX_CODE_ANALYSIS_ASSUME(exp) __analysis_assume(!!(exp))
	#else
		#define PX_CODE_ANALYSIS_ASSUME(exp)
	#endif
#	define PX_ASSERT(exp) { static bool _ignore = false; ((void)((!!(exp)) || (!_ignore && (physx::PxGetAssertHandler()(#exp, __FILE__, __LINE__, _ignore), false)))); PX_CODE_ANALYSIS_ASSUME(exp); }
#	define PX_ALWAYS_ASSERT_MESSAGE(exp) { static bool _ignore = false; if(!_ignore)physx::PxGetAssertHandler()(exp, __FILE__, __LINE__, _ignore); }
#   define PX_ASSERT_WITH_MESSAGE(exp, message) {static bool _ignore = false; ((void)((!!(exp)) || (!_ignore && (physx::PxGetAssertHandler()(message, __FILE__, __LINE__, _ignore), false)))); PX_CODE_ANALYSIS_ASSUME(exp);} 
#else
#	include "ps3/PxPS3Assert.h"
#endif

#define PX_ALWAYS_ASSERT() PX_ASSERT(0)

/** @} */
#endif // PX_FOUNDATION_PX_ASSERT_H

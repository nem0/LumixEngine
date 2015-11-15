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


#ifndef PX_FOUNDATION_PX_WINDOWS_INTRINSICS_H
#define PX_FOUNDATION_PX_WINDOWS_INTRINSICS_H

#include "foundation/Px.h"
#include "foundation/PxAssert.h"

#if !(defined PX_WINDOWS || defined PX_WINMODERN)
	#error "This file should only be included by Windows or WIN8ARM builds!!"
#endif

#include <math.h>
#include <float.h>

#ifndef PX_DOXYGEN
namespace physx
{
namespace intrinsics
{
#endif

	//! \brief platform-specific absolute value
	PX_CUDA_CALLABLE PX_FORCE_INLINE float abs(float a)						{	return ::fabs(a);	}

	//! \brief platform-specific select float
	PX_CUDA_CALLABLE PX_FORCE_INLINE float fsel(float a, float b, float c)	{	return (a >= 0.0f) ? b : c;	}

	//! \brief platform-specific sign
	PX_CUDA_CALLABLE PX_FORCE_INLINE float sign(float a)					{	return (a >= 0.0f) ? 1.0f : -1.0f; }

	//! \brief platform-specific reciprocal
	PX_CUDA_CALLABLE PX_FORCE_INLINE float recip(float a)					{	return 1.0f/a;			}

	//! \brief platform-specific reciprocal estimate
	PX_CUDA_CALLABLE PX_FORCE_INLINE float recipFast(float a)				{	return 1.0f/a;			}

	//! \brief platform-specific square root
	PX_CUDA_CALLABLE PX_FORCE_INLINE float sqrt(float a)					{	return ::sqrtf(a);	}

	//! \brief platform-specific reciprocal square root
	PX_CUDA_CALLABLE PX_FORCE_INLINE float recipSqrt(float a)				{   return 1.0f/::sqrtf(a); }

	//! \brief platform-specific reciprocal square root estimate
	PX_CUDA_CALLABLE PX_FORCE_INLINE float recipSqrtFast(float a)			{	return 1.0f/::sqrtf(a); }

	//! \brief platform-specific sine
	PX_CUDA_CALLABLE PX_FORCE_INLINE float sin(float a)						{   return ::sinf(a); }

	//! \brief platform-specific cosine
	PX_CUDA_CALLABLE PX_FORCE_INLINE float cos(float a)						{   return ::cosf(a); }

	//! \brief platform-specific minimum
	PX_CUDA_CALLABLE PX_FORCE_INLINE float selectMin(float a, float b)		{	return a<b ? a : b;	}

	//! \brief platform-specific maximum
	PX_CUDA_CALLABLE PX_FORCE_INLINE float selectMax(float a, float b)		{	return a>b ? a : b; }

	//! \brief platform-specific finiteness check (not INF or NAN)
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isFinite(float a)
	{
#ifdef __CUDACC__
		return isfinite(a) ? true : false;
#else
		return (0 == ((_FPCLASS_SNAN | _FPCLASS_QNAN | _FPCLASS_NINF | _FPCLASS_PINF) & _fpclass(a) ));
#endif
	}

	//! \brief platform-specific finiteness check (not INF or NAN)
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isFinite(double a)
	{
#ifdef __CUDACC__
		return isfinite(a) ? true : false;
#else
		return (0 == ((_FPCLASS_SNAN | _FPCLASS_QNAN | _FPCLASS_NINF | _FPCLASS_PINF) & _fpclass(a) ));
#endif
	}

	/*!
	Sets \c count bytes starting at \c dst to zero.
	*/
	PX_FORCE_INLINE void* memZero(void* PX_RESTRICT dest, PxU32 count)
	{
		return memset(dest, 0, count);
	}

	/*!
	Sets \c count bytes starting at \c dst to \c c.
	*/
	PX_FORCE_INLINE void* memSet(void* PX_RESTRICT dest, PxI32 c, PxU32 count)
	{
		return memset(dest, c, count);
	}

	/*!
	Copies \c count bytes from \c src to \c dst. User memMove if regions overlap.
	*/
	PX_FORCE_INLINE void* memCopy(void* PX_RESTRICT dest, const void* PX_RESTRICT src, PxU32 count)
	{
		return memcpy(dest, src, count);
	}

	/*!
	Copies \c count bytes from \c src to \c dst. Supports overlapping regions.
	*/
	PX_FORCE_INLINE void* memMove(void* PX_RESTRICT dest, const void* PX_RESTRICT src, PxU32 count)
	{
		return memmove(dest, src, count);
	}

	/*!
	Set 128B to zero starting at \c dst+offset. Must be aligned.
	*/
	PX_FORCE_INLINE void memZero128(void* PX_RESTRICT dest, PxU32 offset = 0)
	{
		PX_ASSERT(((size_t(dest)+offset) & 0x7f) == 0);
		memSet((char* PX_RESTRICT)dest+offset, 0, 128);
	}

#ifndef PX_DOXYGEN
} // namespace intrinsics
} // namespace physx
#endif

#endif

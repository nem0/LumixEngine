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


#ifndef PX_FOUNDATION_PX_MATH_H
#define PX_FOUNDATION_PX_MATH_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxPreprocessor.h"

#ifdef PX_VC
#pragma warning(push)
#pragma warning( disable : 4985 ) // 'symbol name': attributes not present on previous declaration
#endif
#include <math.h>
#ifdef PX_VC
#pragma warning(pop)
#endif

#include <float.h>
#include "foundation/PxIntrinsics.h"
#include "foundation/PxAssert.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

	/** enum for zero constructor tag for vectors and matrices */
	enum PxZERO			{	PxZero		};

	/** enum for identity constructor flag for quaternions, transforms, and matrices */
	enum PxIDENTITY		{	PxIdentity	};


	// constants
	static const PxReal PxPi			=	PxReal(3.141592653589793);
	static const PxReal PxHalfPi		=	PxReal(1.57079632679489661923);
	static const PxReal PxTwoPi			=	PxReal(6.28318530717958647692);
	static const PxReal PxInvPi			=	PxReal(0.31830988618379067154);		
	static const PxReal PxInvTwoPi		=   PxReal(0.15915494309189533577);
	static const PxReal PxPiDivTwo		=   PxReal(1.57079632679489661923);
	static const PxReal PxPiDivFour		=   PxReal(0.78539816339744830962);


	/**
	\brief The return value is the greater of the two specified values. 
	*/
	template<class T>
	PX_CUDA_CALLABLE PX_FORCE_INLINE T PxMax(T a, T b)							{ return a<b ? b : a;	}

	//! overload for float to use fsel on xbox
	template<>
	PX_CUDA_CALLABLE PX_FORCE_INLINE float PxMax(float a, float b)				{ return intrinsics::selectMax(a, b); }

	/**
	\brief The return value is the lesser of the two specified values. 
	*/
	template<class T>
	PX_CUDA_CALLABLE PX_FORCE_INLINE T PxMin(T a, T b)							{ return a<b ? a : b;	}

	template<>
	//! overload for float to use fsel on xbox
	PX_CUDA_CALLABLE PX_FORCE_INLINE float PxMin(float a, float b)				{ return intrinsics::selectMin(a, b); }

	/*
	Many of these are just implemented as PX_CUDA_CALLABLE PX_FORCE_INLINE calls to the C lib right now,
	but later we could replace some of them with some approximations or more
	clever stuff.
	*/

	/**
	\brief abs returns the absolute value of its argument. 
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxAbs(PxF32 a)						{ return intrinsics::abs(a);	}

	PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxEquals(PxF32 a, PxF32 b,PxF32 eps)	{ return (PxAbs(a - b) < eps);	}

	/**
	\brief abs returns the absolute value of its argument. 
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxAbs(PxF64 a)						{ return ::fabs(a);	}

	/**
	\brief abs returns the absolute value of its argument. 
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxI32 PxAbs(PxI32 a)						{ return ::abs(a);	}

	/**
	\brief Clamps v to the range [hi,lo]
	*/
	template<class T>
	PX_CUDA_CALLABLE PX_FORCE_INLINE T PxClamp(T v, T lo, T hi)					{ PX_ASSERT(lo<=hi); return PxMin(hi, PxMax(lo, v)); }

	//!	\brief Square root.
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxSqrt(PxF32 a)						{ return intrinsics::sqrt(a);		}

	//!	\brief Square root.
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxSqrt(PxF64 a)						{ return ::sqrt(a);	}

	//!	\brief reciprocal square root.
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxRecipSqrt(PxF32 a)					{ return intrinsics::recipSqrt(a);	}

	//!	\brief reciprocal square root.
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxRecipSqrt(PxF64 a)					{ return 1/::sqrt(a);	}

	//!trigonometry -- all angles are in radians.

	//!	\brief Sine of an angle ( <b>Unit:</b> Radians )
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxSin(PxF32 a)						{ return intrinsics::sin(a);				}

	//!	\brief Sine of an angle ( <b>Unit:</b> Radians )
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxSin(PxF64 a)						{ return ::sin(a);							}

	//!	\brief Cosine of an angle (<b>Unit:</b> Radians)
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxCos(PxF32 a)						{ return intrinsics::cos(a);				}

	//!	\brief Cosine of an angle (<b>Unit:</b> Radians)
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxCos(PxF64 a)						{ return ::cos(a);							}

	/**
	\brief Tangent of an angle.
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxTan(PxF32 a)						{ return ::tan(a);							}

	/**
	\brief Tangent of an angle.
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxTan(PxF64 a)						{ return ::tan(a);							}

	/**
	\brief Arcsine.
	Returns angle between -PI/2 and PI/2 in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxAsin(PxF32 f)						{ return ::asin(PxClamp(f,-1.0f,1.0f));	}

	/**
	\brief Arcsine.
	Returns angle between -PI/2 and PI/2 in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxAsin(PxF64 f)						{ return ::asin(PxClamp(f,-1.0,1.0));		}

	/**
	\brief Arccosine.
	Returns angle between 0 and PI in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxAcos(PxF32 f)						{ return ::acos(PxClamp(f,-1.0f,1.0f));			}

	/**
	\brief Arccosine.
	Returns angle between 0 and PI in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxAcos(PxF64 f)						{ return ::acos(PxClamp(f,-1.0,1.0));				}

	/**
	\brief ArcTangent.
	Returns angle between -PI/2 and PI/2 in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxAtan(PxF32 a)						{ return ::atan(a);	}

	/**
	\brief ArcTangent.
	Returns angle between -PI/2 and PI/2 in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxAtan(PxF64 a)						{ return ::atan(a);	}

	/**
	\brief Arctangent of (x/y) with correct sign.
	Returns angle between -PI and PI in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxAtan2(PxF32 x, PxF32 y)			{ return ::atan2(x,y);	}

	/**
	\brief Arctangent of (x/y) with correct sign.
	Returns angle between -PI and PI in radians
	<b>Unit:</b> Radians
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF64 PxAtan2(PxF64 x, PxF64 y)			{ return ::atan2(x,y);	}

	//!	\brief returns true if the passed number is a finite floating point number as opposed to INF, NAN, etc.
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxIsFinite(PxF32 f)					{ return intrinsics::isFinite(f);	}

	//!	\brief returns true if the passed number is a finite floating point number as opposed to INF, NAN, etc.
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxIsFinite(PxF64 f)					{ return intrinsics::isFinite(f);	}

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxFloor(PxF32 a)						{ return ::floorf(a);					}

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxExp(PxF32 a)						{ return ::expf(a);	}

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxCeil(PxF32 a)						{ return ::ceilf(a);	}

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxSign(PxF32 a)						{ return physx::intrinsics::sign(a); }

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxPow(PxF32 x,PxF32 y)				{ return ::powf(x,y); }

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxF32 PxLog(PxF32 x)						{ return ::log(x); }

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_FOUNDATION_PX_MATH_H

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


#ifndef PX_FOUNDATION_PX_PLANE_H
#define PX_FOUNDATION_PX_PLANE_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxMath.h"
#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Representation of a plane.

 Plane equation used: n.dot(v) + d = 0
*/
class PxPlane
{
public:
	/**
	\brief Constructor
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxPlane()
	{
	}

	/**
	\brief Constructor from a normal and a distance
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxPlane(PxReal nx, PxReal ny, PxReal nz, PxReal distance)
		: n(nx, ny, nz)
		, d(distance)
	{
	}

	/**
	\brief Constructor from a normal and a distance
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxPlane(const PxVec3& normal, PxReal distance) 
		: n(normal)
		, d(distance)
	{
	}


	/**
	\brief Constructor from a point on the plane and a normal
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxPlane(const PxVec3& point, const PxVec3& normal)
		: n(normal)		
		, d(-point.dot(n))		// p satisfies normal.dot(p) + d = 0
	{
	}

	/**
	\brief Constructor from three points
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxPlane(const PxVec3& p0, const PxVec3& p1, const PxVec3& p2)
	{
		n = (p1 - p0).cross(p2 - p0).getNormalized();
		d = -p0.dot(n);
	}

	/**
	\brief returns true if the two planes are exactly equal
	*/
	PX_CUDA_CALLABLE PX_INLINE bool operator==(const PxPlane& p) const	{ return n == p.n && d == p.d; }

	PX_CUDA_CALLABLE PX_FORCE_INLINE PxReal distance(const PxVec3& p) const
	{
		return p.dot(n) + d;
	}

	PX_CUDA_CALLABLE PX_FORCE_INLINE bool contains(const PxVec3& p) const
	{
		return PxAbs(distance(p)) < (1.0e-7f);
	}

	/**
	\brief projects p into the plane
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 project(const PxVec3 & p) const
	{
		return p - n * distance(p);
	}

	/**
	\brief find an arbitrary point in the plane
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 pointInPlane() const
	{
		return -n*d;
	}

	/**
	\brief equivalent plane with unit normal
	*/

	PX_CUDA_CALLABLE PX_FORCE_INLINE void normalize()
	{
		PxReal denom = 1.0f / n.magnitude();
		n *= denom;
		d *= denom;
	}


	PxVec3	n;			//!< The normal to the plane
	PxReal	d;			//!< The distance from the origin
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif


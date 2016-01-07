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


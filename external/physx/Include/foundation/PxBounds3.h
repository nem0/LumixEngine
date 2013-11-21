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


#ifndef PX_FOUNDATION_PX_BOUNDS3_H
#define PX_FOUNDATION_PX_BOUNDS3_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxTransform.h"
#include "foundation/PxMat33.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Class representing 3D range or axis aligned bounding box.

Stored as minimum and maximum extent corners. Alternate representation
would be center and dimensions.
May be empty or nonempty. If not empty, minimum <= maximum has to hold.
*/
class PxBounds3
{
public:

	/**
	\brief Default constructor, not performing any initialization for performance reason.
	\remark Use empty() function below to construct empty bounds.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3()	{}

	/**
	\brief Construct from two bounding points
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3(const PxVec3& minimum, const PxVec3& maximum);

	/**
	\brief Return empty bounds. 
	*/
	static PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 empty();

	/**
	\brief returns the AABB containing v0 and v1.
	\param v0 first point included in the AABB.
	\param v1 second point included in the AABB.
	*/
	static PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 boundsOfPoints(const PxVec3& v0, const PxVec3& v1);

	/**
	\brief returns the AABB from center and extents vectors.
	\param center Center vector
	\param extent Extents vector
	*/
	static PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 centerExtents(const PxVec3& center, const PxVec3& extent);

	/**
	\brief Construct from center, extent, and (not necessarily orthogonal) basis
	*/
	static PX_CUDA_CALLABLE PX_INLINE PxBounds3 basisExtent(const PxVec3& center, const PxMat33& basis, const PxVec3& extent);

	/**
	\brief Construct from pose and extent
	*/
	static PX_CUDA_CALLABLE PX_INLINE PxBounds3 poseExtent(const PxTransform& pose, const PxVec3& extent);

	/**
	\brief gets the transformed bounds of the passed AABB (resulting in a bigger AABB).
	\param[in] matrix Transform to apply, can contain scaling as well
	\param[in] bounds The bounds to transform.
	*/
	static PX_CUDA_CALLABLE PX_INLINE PxBounds3 transform(const PxMat33& matrix, const PxBounds3& bounds);

	/**
	\brief gets the transformed bounds of the passed AABB (resulting in a bigger AABB).
	\param[in] transform Transform to apply, can contain scaling as well
	\param[in] bounds The bounds to transform.
	*/
	static PX_CUDA_CALLABLE PX_INLINE PxBounds3 transform(const PxTransform& transform, const PxBounds3& bounds);

	/**
	\brief Sets empty to true
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void setEmpty();

	/**
	\brief Sets infinite bounds
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void setInfinite();

	/**
	\brief expands the volume to include v
	\param v Point to expand to.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void include(const PxVec3& v);

	/**
	\brief expands the volume to include b.
	\param b Bounds to perform union with.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void include(const PxBounds3& b);

	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isEmpty() const;

	/**
	\brief indicates whether the intersection of this and b is empty or not.
	\param b Bounds to test for intersection.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool intersects(const PxBounds3& b) const;

	/**
	 \brief computes the 1D-intersection between two AABBs, on a given axis.
	 \param	a		the other AABB
	 \param	axis	the axis (0, 1, 2)
	 */
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool intersects1D(const PxBounds3& a, PxU32 axis)	const;

	/**
	\brief indicates if these bounds contain v.
	\param v Point to test against bounds.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool contains(const PxVec3& v) const;

	/**
	 \brief	checks a box is inside another box.
	 \param	box		the other AABB
	 */
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isInside(const PxBounds3& box) const;

	/**
	\brief returns the center of this axis aligned box.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 getCenter() const;

	/**
	\brief get component of the box's center along a given axis
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE float	getCenter(PxU32 axis)	const;

	/**
	\brief get component of the box's extents along a given axis
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE float	getExtents(PxU32 axis)	const;

	/**
	\brief returns the dimensions (width/height/depth) of this axis aligned box.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 getDimensions() const;

	/**
	\brief returns the extents, which are half of the width/height/depth.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 getExtents() const;

	/**
	\brief scales the AABB.
	\param scale Factor to scale AABB by.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void scale(PxF32 scale);

	/** 
	fattens the AABB in all 3 dimensions by the given distance. 
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE void fatten(PxReal distance);

	/** 
	checks that the AABB values are not NaN
	*/

	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isFinite() const;

	PxVec3 minimum, maximum;
};


PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3::PxBounds3(const PxVec3& minimum, const PxVec3& maximum)
: minimum(minimum), maximum(maximum)
{
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 PxBounds3::empty()
{
	return PxBounds3(PxVec3(PX_MAX_REAL), PxVec3(-PX_MAX_REAL));
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::isFinite() const
	{
	return minimum.isFinite() && maximum.isFinite();
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 PxBounds3::boundsOfPoints(const PxVec3& v0, const PxVec3& v1)
{
	return PxBounds3(v0.minimum(v1), v0.maximum(v1));
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxBounds3 PxBounds3::centerExtents(const PxVec3& center, const PxVec3& extent)
{
	return PxBounds3(center - extent, center + extent);
}

PX_CUDA_CALLABLE PX_INLINE PxBounds3 PxBounds3::basisExtent(const PxVec3& center, const PxMat33& basis, const PxVec3& extent)
{
	// extended basis vectors
	PxVec3 c0 = basis.column0 * extent.x;
	PxVec3 c1 = basis.column1 * extent.y;
	PxVec3 c2 = basis.column2 * extent.z;

	PxVec3 w;
	// find combination of base vectors that produces max. distance for each component = sum of abs()
	w.x = PxAbs(c0.x) + PxAbs(c1.x) + PxAbs(c2.x);
	w.y = PxAbs(c0.y) + PxAbs(c1.y) + PxAbs(c2.y);
	w.z = PxAbs(c0.z) + PxAbs(c1.z) + PxAbs(c2.z);

	return PxBounds3(center - w, center + w);
}

PX_CUDA_CALLABLE PX_INLINE PxBounds3 PxBounds3::poseExtent(const PxTransform& pose, const PxVec3& extent)
{
	return basisExtent(pose.p, PxMat33(pose.q), extent);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::setEmpty()
{
	minimum = PxVec3(PX_MAX_REAL);
	maximum = PxVec3(-PX_MAX_REAL);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::setInfinite()
{
	minimum = PxVec3(-PX_MAX_REAL);
	maximum = PxVec3(PX_MAX_REAL);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::include(const PxVec3& v)
{
	PX_ASSERT(isFinite());
	minimum = minimum.minimum(v);
	maximum = maximum.maximum(v);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::include(const PxBounds3& b)
{
	PX_ASSERT(isFinite());
	minimum = minimum.minimum(b.minimum);
	maximum = maximum.maximum(b.maximum);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::isEmpty() const
{
	PX_ASSERT(isFinite());
	// Consistency condition for (Min, Max) boxes: minimum < maximum
	return minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z;
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::intersects(const PxBounds3& b) const
{
	PX_ASSERT(isFinite() && b.isFinite());
	return !(b.minimum.x > maximum.x || minimum.x > b.maximum.x ||
			 b.minimum.y > maximum.y || minimum.y > b.maximum.y ||
			 b.minimum.z > maximum.z || minimum.z > b.maximum.z);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::intersects1D(const PxBounds3& a, PxU32 axis)	const
{
	PX_ASSERT(isFinite() && a.isFinite());
	return maximum[axis] >= a.minimum[axis] && a.maximum[axis] >= minimum[axis];
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::contains(const PxVec3& v) const
{
	PX_ASSERT(isFinite());

	return !(v.x < minimum.x || v.x > maximum.x ||
		v.y < minimum.y || v.y > maximum.y ||
		v.z < minimum.z || v.z > maximum.z);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE bool PxBounds3::isInside(const PxBounds3& box) const
{
	PX_ASSERT(isFinite() && box.isFinite());
	if(box.minimum.x>minimum.x)	return false;
	if(box.minimum.y>minimum.y)	return false;
	if(box.minimum.z>minimum.z)	return false;
	if(box.maximum.x<maximum.x)	return false;
	if(box.maximum.y<maximum.y)	return false;
	if(box.maximum.z<maximum.z)	return false;
	return true;
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 PxBounds3::getCenter() const
{
	PX_ASSERT(isFinite());
	return (minimum+maximum) * PxReal(0.5);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE float	PxBounds3::getCenter(PxU32 axis)	const
{
	PX_ASSERT(isFinite());
	return (minimum[axis] + maximum[axis]) * PxReal(0.5);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE float	PxBounds3::getExtents(PxU32 axis)	const
{
	PX_ASSERT(isFinite());
	return (maximum[axis] - minimum[axis]) * PxReal(0.5);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 PxBounds3::getDimensions() const
{
	PX_ASSERT(isFinite());
	return maximum - minimum;
}

PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec3 PxBounds3::getExtents() const
{
	PX_ASSERT(isFinite());
	return getDimensions() * PxReal(0.5);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::scale(PxF32 scale)
{
	PX_ASSERT(isFinite());
	*this = centerExtents(getCenter(), getExtents() * scale);
}

PX_CUDA_CALLABLE PX_FORCE_INLINE void PxBounds3::fatten(PxReal distance)
{
	PX_ASSERT(isFinite());
	minimum.x -= distance;
	minimum.y -= distance;
	minimum.z -= distance;

	maximum.x += distance;
	maximum.y += distance;
	maximum.z += distance;
}

PX_CUDA_CALLABLE PX_INLINE PxBounds3 PxBounds3::transform(const PxMat33& matrix, const PxBounds3& bounds)
{
	PX_ASSERT(bounds.isFinite());
	return bounds.isEmpty() ? bounds :
		PxBounds3::basisExtent(matrix * bounds.getCenter(), matrix, bounds.getExtents());
}

PX_CUDA_CALLABLE PX_INLINE PxBounds3 PxBounds3::transform(const PxTransform& transform, const PxBounds3& bounds)
{
	PX_ASSERT(bounds.isFinite());
	return bounds.isEmpty() ? bounds :
		PxBounds3::basisExtent(transform.transform(bounds.getCenter()), PxMat33(transform.q), bounds.getExtents());
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_FOUNDATION_PX_BOUNDS3_H

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


#ifndef PX_PHYSICS_NX_PLANE_GEOMETRY
#define PX_PHYSICS_NX_PLANE_GEOMETRY
/** \addtogroup geomutils
@{
*/
#include "foundation/PxPlane.h"
#include "foundation/PxTransform.h"
#include "geometry/PxGeometry.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Class describing a plane geometry.

The plane geometry specifies the half-space volume x<=0. As with other geometry types, 
when used in a PxShape the collision volume is obtained by transforming the halfspace 
by the shape local pose and the actor global pose.

To generate a PxPlane from a PxTransform, transform PxPlane(1,0,0,0).

To generate a PxTransform from a PxPlane, use PxTransformFromPlaneEquation.

@see PxShape.setGeometry() PxShape.getPlaneGeometry() PxTransformFromPlaneEquation 
*/
class PxPlaneGeometry : public PxGeometry 
{
public:
	PX_INLINE PxPlaneGeometry() :	PxGeometry(PxGeometryType::ePLANE) {}

	/**
	\brief Returns true if the geometry is valid.

	\return True if the current settings are valid
	*/
	PX_INLINE bool isValid() const;
};


PX_INLINE bool PxPlaneGeometry::isValid() const
{
	if (mType != PxGeometryType::ePLANE)
		return false;

	return true;
}


/** \brief creates a transform from a plane equation, suitable for an actor transform for a PxPlaneGeometry

\param[in] plane the desired plane equation
\return a PxTransform which will transform the plane PxPlane(1,0,0,0) to the specified plane
*/

PX_FOUNDATION_API PxTransform PxTransformFromPlaneEquation(const PxPlane& plane);

/** \brief creates a plane equation from a transform, such as the actor transform for a PxPlaneGeometry

\param[in] transform the transform
\return the plane
*/


PX_INLINE PxPlane PxPlaneEquationFromTransform(const PxTransform& transform)
{
	return transform.transform(PxPlane(1.f,0.f,0.f,0.f));
}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

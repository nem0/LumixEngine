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


#ifndef PX_PHYSICS_NX_SPHERE_GEOMETRY
#define PX_PHYSICS_NX_SPHERE_GEOMETRY
/** \addtogroup geomutils
@{
*/
#include "geometry/PxGeometry.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A class representing the geometry of a sphere.

Spheres are defined by their radius.
\note The scaling of the sphere is expected to be baked into this value, there is no additional scaling parameter.
*/
class PxSphereGeometry : public PxGeometry 
{
public:
	PX_INLINE PxSphereGeometry() :							PxGeometry(PxGeometryType::eSPHERE), radius(0) {}
	PX_INLINE PxSphereGeometry(PxReal ir) :					PxGeometry(PxGeometryType::eSPHERE), radius(ir) {}

	/**
	\brief Returns true if the geometry is valid.

	\return True if the current settings are valid

	\note A valid sphere has radius > 0.  
	It is illegal to call PxRigidActor::createShape and PxPhysics::createShape with a sphere that has zero radius.

	@see PxRigidActor::createShape, PxPhysics::createShape
	*/
	PX_INLINE bool isValid() const;

public:

	/**
	\brief The radius of the sphere.
	*/
	PxReal radius;	
};


PX_INLINE bool PxSphereGeometry::isValid() const
{
	if (mType != PxGeometryType::eSPHERE)
		return false;
	if (!PxIsFinite(radius))
		return false;
	if (radius <= 0.0f)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

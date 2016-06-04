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

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


#ifndef PX_PHYSICS_NX_CLOTH_COLLISION_DATA
#define PX_PHYSICS_NX_CLOTH_COLLISION_DATA
/** \addtogroup cloth
  @{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Sphere representation used for cloth-sphere and cloth-capsule collision.
\details Cloth can collide with spheres and capsules.  Each capsule is represented by
a pair of spheres with possibly different radii.
*/
struct PxClothCollisionSphere
{
	PxVec3 pos;    //!< position of the sphere
	PxReal radius; //!< radius of the sphere.

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothCollisionSphere() {}
	PxClothCollisionSphere(const PxVec3& p, PxReal r)
		: pos(p), radius(r) {}
};

/**
\brief Plane representation used for cloth-convex collision.
\details Cloth can collide with convexes.  Each convex is represented by
a mask of the planes that make up the convex.
*/
struct PxClothCollisionPlane
{
	PxVec3 normal;   //!< The normal to the plane
	PxReal distance; //!< The distance to the origin (in the normal direction)

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothCollisionPlane() {}
	PxClothCollisionPlane(const PxVec3& normal_, PxReal distance_)
		: normal(normal_), distance(distance_) {}
};

/**
\brief Triangle representation used for cloth-mesh collision.
*/
struct PxClothCollisionTriangle
{
	PxVec3 vertex0;
	PxVec3 vertex1;
	PxVec3 vertex2;

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothCollisionTriangle() {}
	PxClothCollisionTriangle(
		const PxVec3& v0, 
		const PxVec3& v1,
		const PxVec3& v2) :
		vertex0(v0),
		vertex1(v1),
		vertex2(v2) {}
};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

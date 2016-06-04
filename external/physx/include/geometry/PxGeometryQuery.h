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


#ifndef PX_PHYSICS_GEOMUTILS_PX_GEOMETRY_QUERY
#define PX_PHYSICS_GEOMUTILS_PX_GEOMETRY_QUERY

/**
\brief Maximum sweep distance for scene sweeps. The distance parameter for sweep functions will be clamped to this value.
The reason for this is GJK support cannot be evaluated near infinity. A viable alternative can be a sweep followed by an infinite raycast.

@see PxScene
*/
#define PX_MAX_SWEEP_DISTANCE 1e8f

/** \addtogroup geomutils
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "PxQueryReport.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxGeometry;
struct PxSweepHit;
struct PxRaycastHit;

class PxTriangle;

/**
\brief Collection of geometry object queries (sweeps, raycasts, overlaps, ...).
*/
class PxGeometryQuery
{
public:

	/**
	\brief Sweep a specified geometry object in space and test for collision with a given object.

	The following combinations are supported.

	\li PxSphereGeometry vs. {PxSphereGeometry, PxPlaneGeometry, PxCapsuleGeometry, PxBoxGeometry, PxConvexMeshGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxCapsuleGeometry vs. {PxSphereGeometry, PxPlaneGeometry, PxCapsuleGeometry, PxBoxGeometry, PxConvexMeshGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxBoxGeometry vs. {PxSphereGeometry, PxPlaneGeometry, PxCapsuleGeometry, PxBoxGeometry, PxConvexMeshGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxConvexMeshGeometry vs. {PxSphereGeometry, PxPlaneGeometry, PxCapsuleGeometry, PxBoxGeometry, PxConvexMeshGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}

	\param[in] unitDir Normalized direction along which object geom0 should be swept
	\param[in] maxDist Maximum sweep distance, has to be in the [0, inf) range
	\param[in] geom0 The geometry object to sweep. Supported geometries are #PxSphereGeometry, #PxCapsuleGeometry, #PxBoxGeometry and #PxConvexMeshGeometry
	\param[in] pose0 Pose of the geometry object to sweep
	\param[in] geom1 The geometry object to test the sweep against
	\param[in] pose1 Pose of the geometry object to sweep against
	\param[out] sweepHit The sweep hit information. Only valid if this method returns true.
	\param[in] hitFlags Specify which properties per hit should be computed and written to result hit array. Combination of #PxHitFlag flags
	\param[in] inflation Surface of the swept shape is additively extruded in the normal direction, rounding corners and edges.

	\return True if the swept geometry object geom0 hits the object geom1

	@see PxSweepHit PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static bool sweep(const PxVec3& unitDir, 
							const PxReal maxDist,
							const PxGeometry& geom0,
							const PxTransform& pose0,
							const PxGeometry& geom1,
							const PxTransform& pose1,
							PxSweepHit& sweepHit,
							PxHitFlags hitFlags = PxHitFlag::eDEFAULT,
							const PxReal inflation = 0.f);


	/**
	\brief Overlap test for two geometry objects.

	All combinations are supported except:
	\li PxPlaneGeometry vs. {PxPlaneGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxTriangleMeshGeometry vs. {PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxHeightFieldGeometry vs. {PxHeightFieldGeometry}

	\param[in] geom0 The first geometry object
	\param[in] pose0 Pose of the first geometry object
	\param[in] geom1 The second geometry object
	\param[in] pose1 Pose of the second geometry object
	\return True if the two geometry objects overlap

	@see PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static bool overlap(const PxGeometry& geom0, const PxTransform& pose0,
											const PxGeometry& geom1, const PxTransform& pose1);


	/**
	\brief Raycast test against a geometry object.

	\param[in] origin The origin of the ray to test the geometry object against
	\param[in] unitDir Normalized direction of the ray to test the geometry object against
	\param[in] geom The geometry object to test the ray against
	\param[in] pose Pose of the geometry object
	\param[in] maxDist Maximum ray length, has to be in the [0, inf) range
	\param[in] hitFlags Specification of the kind of information to retrieve on hit. Combination of #PxHitFlag flags
	\param[in] maxHits max number of returned hits = size of 'rayHits' buffer
	\param[out] rayHits Raycast hits information
	\param[in] anyHit Set to false if the closest hit point should be computed, else the query aborts as soon as any hit point along the ray is found.
	\return Number of hits between the ray and the geometry object

	\note PX_DEPRECATED: The 'anyHit' parameter is deprecated. Please use PxHitFlag::eMESH_ANY instead.

	@see PxRaycastHit PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static PxU32 raycast(const PxVec3& origin,
							const PxVec3& unitDir,
							const PxGeometry& geom,
							const PxTransform& pose,
							PxReal maxDist,
							PxHitFlags hitFlags,
							PxU32 maxHits,
							PxRaycastHit* PX_RESTRICT rayHits,
							bool anyHit = false);

	/**
	\brief Compute minimum translational distance (MTD) between two geometry objects.

	All combinations of geom objects are supported except:
	- plane/plane
	- plane/mesh
	- plane/heightfield
	- mesh/mesh
	- mesh/heightfield
	- heightfield/heightfield

	The function returns a unit vector ('direction') and a penetration depth ('depth').

	The depenetration vector D = direction * depth should be applied to the first object, to
	get out of the second object.

	Returned depth should always be positive or null.

	If objects do not overlap, the function can not compute the MTD and returns false.

	\param[out] direction Computed MTD unit direction
	\param[out] depth Penetration depth. Always positive or null.
	\param[in] geom0 The first geometry object
	\param[in] pose0 Pose of the first geometry object
	\param[in] geom1 The second geometry object
	\param[in] pose1 Pose of the second geometry object
	\return True if the MTD has successfully been computed, i.e. if objects do overlap.

	@see PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static bool		computePenetration(PxVec3& direction, PxF32& depth,
											const PxGeometry& geom0, const PxTransform& pose0,
											const PxGeometry& geom1, const PxTransform& pose1);

	/**
	\brief Computes distance between a point and a geometry object.

	Currently supported geometry objects: box, sphere, capsule, convex.

	\param[in] point The point P
	\param[in] geom The geometry object
	\param[in] pose Pose of the geometry object
	\param[out] closestPoint Optionally returned closest point to P on the geom object. Only valid when returned distance is strictly positive.
	\return Square distance between the point and the geom object, or 0.0 if the point is inside the object, or -1.0 if the geometry type is not supported.

	@see PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static PxReal pointDistance(const PxVec3& point, const PxGeometry& geom, const PxTransform& pose, PxVec3* closestPoint=NULL);


	/**
	\brief get the bounds for a geometry object

	\param[in] geom The geometry object
	\param[in] pose Pose of the geometry object
	\param[in] inflation  Scale factor for computed world bounds. Box extents are multiplied by this value.
	\return The bounds of the object

	@see PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static PxBounds3 getWorldBounds(const PxGeometry& geom, const PxTransform& pose, float inflation=1.01f);
};


#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif

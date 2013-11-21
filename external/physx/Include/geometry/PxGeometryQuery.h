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


#ifndef PX_PHYSICS_GEOMUTILS_PX_GEOMETRY_QUERY
#define PX_PHYSICS_GEOMUTILS_PX_GEOMETRY_QUERY

/** \addtogroup geomutils
  @{
*/

#include "common/PxPhysXCommon.h"
#include "PxSceneQueryReport.h"

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

	\param[in] unitDir Normalized direction along which object geom0 should be swept.
	\param[in] distance Sweep distance. Needs to be larger than 0.
	\param[in] geom0 The geometry object to sweep. Supported geometries are #PxSphereGeometry, #PxCapsuleGeometry, #PxBoxGeometry and #PxConvexMeshGeometry
	\param[in] pose0 Pose of the geometry object to sweep
	\param[in] geom1 The geometry object to test the sweep against
	\param[in] pose1 Pose of the geometry object to sweep against
	\param[out] sweepHit The sweep hit information. Only valid if this method returns true.
	\param[in] hintFlags Specification of the kind of information to retrieve on hit. Combination of #PxSceneQueryFlag flags
	\return True if the swept geometry object geom0 hits the object geom1

	@see PxSweepHit PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static bool sweep(const PxVec3& unitDir, 
							const PxReal distance,
							const PxGeometry& geom0,
							const PxTransform& pose0,
							const PxGeometry& geom1,
							const PxTransform& pose1,
							PxSweepHit& sweepHit,
							PxSceneQueryFlags hintFlags=(PxSceneQueryFlags)0xffffffff);


	/**
	\brief Overlap test for two geometry objects.

	All combinations are supported except:
	\li PxPlaneGeometry vs. {PxPlaneGeometry, PxTriangleMeshGeometry, PxHeightFieldGeometry}
	\li PxConvexMeshGeometry vs. {PxTriangleMeshGeometry}
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

	\param[in] rayOrigin The origin of the ray to test the geometry object against
	\param[in] rayDir The direction of the ray to test the geometry object against
	\param[in] geom The geometry object to test the ray against
	\param[in] pose Pose of the geometry object
	\param[in] maxDist Maximum ray length
	\param[in] hintFlags Specification of the kind of information to retrieve on hit. Combination of #PxSceneQueryFlag flags
	\param[in] maxHits max number of returned hits = size of 'rayHits' buffer
	\param[out] rayHits Raycast hits information
	\param[in] firstHit Set to false if the closest hit point should be computed, else the query aborts as soon as the first valid hit point is found.
	\return Number of hits between the ray and the geometry object

	@see PxRaycastHit PxGeometry PxTransform
	*/
	PX_PHYSX_COMMON_API static PxU32 raycast(const PxVec3& rayOrigin,
							const PxVec3& rayDir,
							const PxGeometry& geom,
							const PxTransform& pose,
							PxReal maxDist,
							PxSceneQueryFlags hintFlags,
							PxU32 maxHits,
							PxRaycastHit* PX_RESTRICT rayHits,
							bool firstHit = false);

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
};


#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif

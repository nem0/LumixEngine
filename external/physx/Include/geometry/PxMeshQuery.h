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


#ifndef PX_PHYSICS_GEOMUTILS_PX_MESH_QUERY
#define PX_PHYSICS_GEOMUTILS_PX_MESH_QUERY

/** \addtogroup geomutils
  @{
*/

#include "geometry/PxPhysXGeomUtils.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxGeometry;
class PxConvexMeshGeometry;
class PxTriangleMeshGeometry;
class PxHeightFieldGeometry;

class PxTriangle;


class PxMeshQuery
{
public:

	/**
	\brief Retrieves triangle data from a triangle ID.

	This function can be used together with #findOverlapTriangleMesh() to retrieve triangle properties.

	\param[in] triGeom Geometry of the triangle mesh to extract the triangle from.
	\param[in] transform Transform for the triangle mesh
	\param[in] triangleIndex The index of the triangle to retrieve.
	\param[out] triangle Triangle points in world space.
	\param[out] vertexIndices Returned vertex indices for given triangle

	@see PxTriangle PxTriangleFlags PxTriangleID findOverlapTriangleMesh()
	*/
	PX_PHYSX_COMMON_API static void getTriangle(const PxTriangleMeshGeometry& triGeom, const PxTransform& transform, PxTriangleID triangleIndex, PxTriangle& triangle, PxU32* vertexIndices=NULL);


	/**
	\brief Retrieves triangle data from a triangle ID.

	This function can be used together with #findOverlapHeightField() to retrieve triangle properties.

	\param[in] hfGeom Geometry of the height field to extract the triangle from.
	\param[in] transform Transform for the height field.
	\param[in] triangleIndex The index of the triangle to retrieve.
	\param[out] triangle Triangle points in world space.
	\param[out] vertexIndices Returned vertex indices for given triangle

	@see PxTriangle PxTriangleFlags PxTriangleID findOverlapHeightField()
	*/
	PX_PHYSX_COMMON_API static void getTriangle(const PxHeightFieldGeometry& hfGeom, const PxTransform& transform, PxTriangleID triangleIndex, PxTriangle& triangle, PxU32* vertexIndices=NULL);


	/**
	\brief Find the mesh triangles which touch the specified geometry object.

	Returned triangle indices can be used with #getTriangle() to retrieve the triangle properties.

	\param[in] geom0 The geometry object to test for mesh triangle overlaps. Supported geometries are #PxSphereGeometry, #PxCapsuleGeometry and #PxBoxGeometry
	\param[in] pose0 Pose of the geometry object
	\param[in] geom1 The triangle mesh geometry to check overlap against
	\param[in] pose1 Pose of the triangle mesh
	\param[out] results Indices of overlapping triangles
	\param[in] maxResults Size of 'results' buffer
	\param[in] startIndex Index of first result to be retrieved. Previous indices are skipped.
	\param[out] overflow True if a buffer overflow occured
	\return Number of overlaps found, i.e. number of elements written to the results buffer

	@see PxTriangleMeshGeometry getTriangle()
	*/
	PX_PHYSX_COMMON_API static PxU32 findOverlapTriangleMesh(const PxGeometry& geom0, const PxTransform& pose0,
									const PxTriangleMeshGeometry& geom1, const PxTransform& pose1,
									PxU32* results, PxU32 maxResults, PxU32 startIndex, bool& overflow);

	/**
	\brief Find the height field triangles which touch the specified geometry object.

	Returned triangle indices can be used with #getTriangle() to retrieve the triangle properties.

	\param[in] geom0 The geometry object to test for height field overlaps. Supported geometries are #PxBoxGeometry
	\param[in] pose0 Pose of the geometry object
	\param[in] geom1 The height field geometry to check overlap against
	\param[in] pose1 Pose of the height field
	\param[out] results Indices of overlapping triangles
	\param[in] maxResults Size of 'results' buffer
	\param[in] startIndex Index of first result to be retrieved. Previous indices are skipped.
	\param[out] overflow True if a buffer overflow occured
	\return Number of overlaps found, i.e. number of elements written to the results buffer

	@see PxHeightFieldGeometry getTriangle()
	*/
	PX_PHYSX_COMMON_API static PxU32 findOverlapHeightField(const PxGeometry& geom0,
								   const PxTransform& pose0,
								   const PxHeightFieldGeometry& geom1,
								   const PxTransform& pose1,
								   PxU32* results, PxU32 maxResults, PxU32 startIndex, bool& overflow);
};


#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif

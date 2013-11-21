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


#ifndef PX_PHYSICS_NX_SCENEQUERY
#define PX_PHYSICS_NX_SCENEQUERY
/** \addtogroup scenequery 
@{ */

#include "PxPhysX.h"
#include "PxShape.h"
#include "PxBatchQueryDesc.h"
#include "PxSceneQueryReport.h"
#include "PxSceneQueryFiltering.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxBoxGeometry;
class PxSphereGeometry;
class PxSweepCache;
struct PxSceneQueryCache;

/**
\brief Batched queries object. This is used to perform several queries at the same time. 

@see PxSceneQueryManager
*/
class PxBatchQuery
	{
	public:

	/**
	\brief Executes batched queries.
	*/
	virtual	void							execute()						= 0;

	/**
	\brief Gets the prefilter shader in use for this scene query.

	\return Prefilter shader.

	@see PxBatchQueryDesc.preFilterShade PxBatchQueryPreFilterShader
	*/
	virtual	PxBatchQueryPreFilterShader getPreFilterShader() const			= 0;

	/**
	\brief Gets the postfilter shader in use for this scene query.

	\return Postfilter shader.

	@see PxBatchQueryDesc.preFilterShade PxBatchQueryPostFilterShader
	*/
	virtual	PxBatchQueryPostFilterShader getPostFilterShader() const		= 0;


	/**
	\brief Gets the shared global filter data in use for this scene query.

	\return Shared filter data for filter shader.

	@see getFilterShaderDataSize() PxBatchQueryDesc.filterShaderData PxBatchQueryPreFilterShader, PxBatchQueryPostFilterShader
	*/
	virtual	const void*						getFilterShaderData() const		= 0;

	/**
	\brief Gets the size of the shared global filter data (#PxSceneDesc.filterShaderData)

	\return Size of shared filter data [bytes].

	@see getFilterShaderData() PxBatchQueryDesc.filterShaderDataSize PxBatchQueryPreFilterShader, PxBatchQueryPostFilterShader
	*/
	virtual	PxU32							getFilterShaderDataSize() const	= 0;


	/**
	\brief Retrieves the client specified with PxBatchQueryDesc::ownerClient at creation time.

	It is not possible to change this value after creating the scene query.

	@see PxBatchQueryDesc::ownerClient
	*/
	virtual PxClientID						getOwnerClient() const = 0;

	/**
	\brief Releases PxBatchQuery from PxSceneQueryManager

	@see PxSceneQueryManager
	*/
	virtual	void							release()						= 0;

	/**
	\brief Returns whether any object of type objectsType is hit along the ray.
	
	\note Make certain that the direction vector of the ray is normalized.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[in] filterData	filterData which is passed to the filer shader. See #PxSceneQueryFilterData #PxBatchQueryPreFilterShader, #PxBatchQueryPostFilterShader
	\param[in] userData		user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first. If no hit is found the ray gets queried against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	
	@see PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxRaycastHit raycastSingle() raycastMultiple() 
	*/
	virtual void raycastAny( const PxVec3& origin, const PxVec3& unitDir, PxReal distance = PX_MAX_F32,
							 const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(), 
							 void* userData = NULL,
							 const PxSceneQueryCache* cache = NULL) const = 0;

	/**
	\brief Returns the first object of type objectsType that is hit along the ray.
	
	The world space intersection point, and the distance along the ray are also provided.
	hintFlags is a combination of #PxSceneQueryFlag flags.

	\note Make certain that the direction vector of the ray is normalized.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[in] outputFlags	Specifies which properties should be written to the hit information
	\param[in] filterData	filterData which is passed to the filer shader. See #PxSceneQueryFilterData #PxBatchQueryPreFilterShader, #PxBatchQueryPostFilterShader
	\param[in] userData		user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	
	@see PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxRaycastHit raycastAny() raycastMultiple() 
	*/
	virtual void raycastSingle( const PxVec3& origin, const PxVec3& unitDir, PxReal distance = PX_MAX_F32,
								const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(), 
								PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
								void* userData = NULL,
								const PxSceneQueryCache* cache = NULL) const = 0;


	/**
	\brief Find all objects of type objectsType which a ray intersects.

	hintFlags is a combination of #PxSceneQueryFlag flags.

	\note Make certain that the direction vector of the ray is normalized.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[in] outputFlags	Specifies which properties should be written to the hit information
	\param[in] filterData	filterData which is passed to the filer shader. See #PxSceneQueryFilterData #PxBatchQueryPreFilterShader, #PxBatchQueryPostFilterShader
	\param[in] userData		user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	
	@see PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxRaycastHit raycastAny() raycastSingle() 
	*/
	virtual void raycastMultiple( const PxVec3& origin, const PxVec3& unitDir, PxReal distance = PX_MAX_F32,
								  const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(), 
								  PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
								  void* userData = NULL,
								  const PxSceneQueryCache* cache = NULL) const = 0;


	/**
	\brief Test overlap between a geometry and objects in the scene. Returns all objects of type objectsType which overlap the world-space sphere
	
	\note Filtering: Overlap tests do not distinguish between touching and blocking hit types (see #PxSceneQueryHitType). Both get written to the hit buffer.

	\note PxSceneQueryFilterFlag::eMESH_MULTIPLE and PxSceneQueryFilterFlag::eBACKFACE have no effect in this case

	\param[in] geometry			Geometry of object to check for overlap (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the object.
	\param[in] filterData		Filtering data and simple logic. See #PxSceneQueryFilterData #PxBatchQueryPreFilterShader, #PxBatchQueryPostFilterShader
	\param[in] userData			user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache			Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] maxShapes		user-defined limit on number of reported shapes (0 to report all shapes)

	@see PxSceneQueryFlags PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader 
	*/
	virtual void overlapMultiple( const PxGeometry& geometry,
								  const PxTransform& pose,
								  const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
								  void* userData=NULL,
								  const PxSceneQueryCache* cache = NULL, 
								  PxU32 maxShapes=0) const = 0;

	/**
	\brief Test returning, for a given geometry, any overlapping object in the scene. Returns any object of type objectsType which overlap the world-space sphere
	
	\note Filtering: Overlap tests do not distinguish between touching and blocking hit types (see #PxSceneQueryHitType). Both get written to the hit buffer.

	\note PxSceneQueryFilterFlag::eMESH_MULTIPLE and PxSceneQueryFilterFlag::eBACKFACE have no effect in this case

	\param[in] geometry			Geometry of object to check for overlap (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the object.
	\param[in] filterData		Filtering data and simple logic. See #PxSceneQueryFilterData #PxBatchQueryPreFilterShader, #PxBatchQueryPostFilterShader
	\param[in] userData			user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache			Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.

	@see PxSceneQueryFlags PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader 
	*/
	PX_INLINE void overlapAny( const PxGeometry& geometry,
								  const PxTransform& pose,
								  const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
								  void* userData=NULL,
								  const PxSceneQueryCache* cache = NULL ) 
	{ overlapMultiple(geometry, pose, filterData, userData, cache,1); }

	/**
	\brief Sweeps returning a single result.
	
	Returns the first rigid actor that is hit along the ray. Data for a blocking hit will be returned as specified by the outputFlags field. Touching hits will be ignored.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometry		Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose			Pose of the sweep object.
	\param[in] unitDir		Normalized direction of the sweep.
	\param[in] distance		Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags	Specifies which properties should be written to the hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query

	@see PxSceneQueryFlags PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSweepHit
	*/
	virtual void sweepSingle( const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
							  PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
							  const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							  void* userData=NULL,
							  const PxSceneQueryCache* cache = NULL) const = 0;

	/**
	\brief Performs a linear sweep through space with a compound of geometry objects

	\note Supported geometries are: PxBoxGeometry, PxSphereGeometry, PxCapsuleGeometry, PxConvexMeshGeometry.
	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	The function sweeps all specified geometry objects through space and reports any objects in the scene
	which intersect. Apart from the number of objects intersected in this way, and the objects
	intersected, information on the closest intersection is put in an #PxSweepHit structure which 
	is stored in the user allocated buffer in PxBatchQueryDesc . See #PxBatchQueryDesc.

	\param[in] geometryList List of pointers to the geometry objects to sweep
	\param[in] poseList The world pose for each geometry object
	\param[in] filterDataList Filter data for each geometry object. NULL, if no filtering should be done
	\param[in] geometryCount Number of geometry objects specified
	\param[in] unitDir Normalized direction of the sweep.
	\param[in] distance Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] filterFlags Filter logic settings. See #PxSceneQueryFilterFlag.
	\param[in] outputFlags Allows the user to specify which field of #PxSweepHit they are interested in. See #PxSceneQueryFlag
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache Sweep cache to use with the query

	@see PxSceneQueryFilterFlag PxFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSceneQueryReport PxSweepHit linearCompoundGeometrySweepMultiple
	*/
	virtual	void linearCompoundGeometrySweepSingle( const PxGeometry** geometryList, 
													const PxTransform* poseList, 
													const PxFilterData* filterDataList, 
													PxU32 geometryCount, 
													const PxVec3& unitDir, 
													const PxReal distance, 
													PxSceneQueryFilterFlags filterFlags, 
													PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
													void* userData=NULL,
													const PxSweepCache* cache = NULL) const = 0;



	/**
	\brief Sweep returning multiple results.
	
	Find all rigid actors that get hit along the sweep. Each result contains data as specified by the outputFlags field.

	\note Touching hits are not ordered.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometry		Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose			Pose of the sweep object.
	\param[in] unitDir		Normalized direction of the sweep.
	\param[in] distance		Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags	Specifies which properties should be written to the hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query

	@see PxSceneQueryFlags PxSceneQueryFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSweepHit
	*/
	virtual void sweepMultiple( const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
							    PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
							    const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							    void* userData=NULL,
								const PxSceneQueryCache* cache = NULL) const = 0;


	
	/**
	\brief Performs a linear sweep through space with a compound of geometry objects, returning all overlaps.

	\note Supported geometries are: PxBoxGeometry, PxSphereGeometry, PxCapsuleGeometry, PxConvexMeshGeometry.
	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	The function sweeps all specified geometry objects through space and reports all objects in the scene
	which intersect. Apart from the number of objects intersected in this way, and the objects
	intersected, information on the closest intersection is put in an #PxSweepHit structure which 
	is stored in the user allocated buffer in PxBatchQueryDesc . See #PxBatchQueryDesc.

	\param[in] geometryList List of pointers to the geometry objects to sweep
	\param[in] poseList The world pose for each geometry object
	\param[in] filterDataList Filter data for each geometry object. NULL, if no filtering should be done
	\param[in] geometryCount Number of geometry objects specified
	\param[in] unitDir Normalized direction of the sweep.
	\param[in] distance Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] filterFlags Filter logic settings. See #PxSceneQueryFilterFlag.
	\param[in] outputFlags Allows the user to specify which field of #PxSweepHit they are interested in. See #PxSceneQueryFlag
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query
	\param[in] cache Sweep cache to use with the query

	@see PxSceneQueryFilterFlag PxFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSceneQueryReport PxSweepHit linearCompoundGeometrySweepSingle
	*/
	virtual	void linearCompoundGeometrySweepMultiple( const PxGeometry** geometryList, 
													  const PxTransform* poseList, 
													  const PxFilterData* filterDataList, 
													  PxU32 geometryCount, 
													  const PxVec3& unitDir, 
													  const PxReal distance, 
													  PxSceneQueryFilterFlags filterFlags, 
													  PxSceneQueryFlags outputFlags = PxSceneQueryFlag::eIMPACT|PxSceneQueryFlag::eNORMAL|PxSceneQueryFlag::eDISTANCE|PxSceneQueryFlag::eUV,
													  void* userData=NULL, 
													  const PxSweepCache* cache=NULL) const = 0;


	protected:
	virtual	~PxBatchQuery(){}
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

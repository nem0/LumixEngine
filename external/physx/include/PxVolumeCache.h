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

#ifndef PX_PHYSICS_NX_VOLUMECACHE
#define PX_PHYSICS_NX_VOLUMECACHE
/** \addtogroup physics
@{
*/

#include "PxScene.h"
#include "geometry/PxGeometryHelpers.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Volumetric cache for local collision geometry.

Provides a mechanism for caching objects within a specified volume and performing raycast, sweep, overlap and forEach queries on the cached objects.

@see PxScene.createVolumeCache()
*/
class PxVolumeCache
{
public:
	/**
	\brief A callback wrapper class for use with PxVolumeCache::forEach function.

	Works in tandem with #forEach(). Used to return shapes overlapping with the cache volume (set using the last fill() call) to the user.

	@see PxVolumeCache::forEach
	*/
    struct Iterator
    {
		/**
		\brief Reports cache contents to the user.

		\param[in] count			The number of shapes returned with this call.
		\param[in] actorShapePairs	Array of PxActorShape pairs.

		@see PxVolumeCache::forEach
		*/
		virtual void processShapes(PxU32 count, const PxActorShape* actorShapePairs) = 0;

		/**
		/brief Called after the last processShapes was called.

		@see PxVolumeCache::Iterator::processShapes
		*/
		virtual void finalizeQuery() {}
	protected:
		virtual ~Iterator() {}
    };


	/**
	\brief describes fill() return status.

	@see PxVolumeCache.fill() 
	*/
	enum FillStatus
	{
		/**
		\brief Cache fill() operation was successful, the cache is valid and had enough capacity to store all the objects within the specified cacheVolume.

		*/
		FILL_OK,

		/**
		\brief Over specified cache capacity.

		Cache fill() was over max count specified in PxScene.createVolumeCache() or #setMaxNbStaticShapes() and #setMaxNbDynamicShapes().
		If this value is returned the cache will be in invalid state (no caching), but all the queries will still return correct results within the specified cache volume.

		@see PxScene.createVolumeCache() #setMaxNbStaticShapes() #setMaxNbDynamicShapes()
		*/
		FILL_OVER_MAX_COUNT,

		/**
		\brief Unsupported geometry type.

		The geometry type of cacheVolume parameter provided to #fill() is not supported. Supported types are sphere, box, capsule.

		*/
		FILL_UNSUPPORTED_GEOMETRY_TYPE,

		/**
		\brief Cache fill() ran out of temporary memory for intermediate results, try reducing the cache size.

		*/
		FILL_OUT_OF_MEMORY
	};

	/**
	\brief Fills the cache with objects intersecting the specified cacheVolume.
	
	\param[in] cacheVolume		Geometry of the cached volume (supported types are: sphere, box, capsule).
	\param[in] pose				Pose of the cache volume.

	\return a #FillStatus enum.

	@see PxVolumeCache.FillStatus
	*/
	virtual FillStatus fill(const PxGeometry& cacheVolume, const PxTransform& pose) = 0;
	
	/**
	\brief Checks if the cache is valid (not over specified max capacity, for both statics and dynamics) and up-to-date.
	
	\return True if the cache is valid and up-to-date. Cache can become out-of-date if any statics or dynamics are moved or added or deleted from the scene.

	@see PxVolumeCache.FillStatus
	*/
	virtual bool isValid() const = 0;

	/**
	\brief Invalidates the cache.
	
	Marks the cache as invalid. Subsequent query will attempt to refill the cache from the scene.

	*/
	virtual void invalidate() = 0;

	/**
	\brief Retrieves the last cached volume geometry.

	\return False if the cache wasn't previously filled. True otherwise with cacheVolume from the last fill() call returned in resultVolume and corresponding transform in resultPose.

	@see #fill()
	*/
	virtual bool getCacheVolume(PxGeometryHolder& resultVolume, PxTransform& resultPose) = 0;

	/**
	\brief Returns the total number of cached shapes

	\return The number of shapes stored in the cache (statics+dynamics). Returns -1 if the cache is invalid.
	*/
	virtual PxI32 getNbCachedShapes() = 0;

	/**
	\brief Releases the cache object and its resources.

	@see PxScene.createVolumeCache
	*/
	virtual void release() = 0;

	/**
	\brief Iterates over the scene shapes overlapping with the cache volume.

	forEach will invoke PxVolumeCache::Iterator::processShapes virtual function, returning all overlapping shapes (possibly by issuing multiple callbacks) to the user. The size of reported blocks can change depending on internal SDK implementation. Any pointers to the contents of the buffer are only valid within the scope of a single processShapes() callback function.
	If forEach is invoked on an invalid cache (empty or out of date), this call will attempt to refill the cache within specified capacity. If the cache is over capacity, an attempt will be made to allocate a temp internal buffer, retrieve the results directly from the scene and return to the user via provided iterator.
	Results from forEach will be current for the last set cacheVolume provided in fill() even if the cache is invalid and refill fails. If the number of overlapping shapes is so high that the internal temporary allocation fails this call will produce an error and return.
	
	\param iter		Iterator callback. forEach() will invokes iter.shapes() function (possibly multiple times) to return blocks of actor+shape pairs overlapped with cacheVolume to the user.
	*/
	virtual void forEach(Iterator& iter) = 0; 

	/**
	\brief Sets the limit on the maximum number of static shapes allowed to be stored in the cache.

	If the number of cached objects goes over this limit, the query functions (forEach/raycast/sweep/overlap) will fall back to scene queries.

	\param maxCount		Maximum number of static shapes cached.
	*/
	virtual void setMaxNbStaticShapes(PxU32 maxCount) = 0;

	/**
	\brief Sets the limit on the maximum number of dynamic shapes allowed to be stored in the cache.

	If the number of cached objects goes over this limit, the query functions (forEach/raycast/sweep/overlap) will fall back to scene queries.

	\param maxCount		Maximum number of dynamic shapes cached.
	*/
	virtual void setMaxNbDynamicShapes(PxU32 maxCount) = 0;

	/**
	\brief Returns the current maximum number of static cached shapes.
	
	\return The max number of cached statics.
	*/
	virtual PxU32 getMaxNbStaticShapes() = 0;

	/**
	\brief Returns the current maximum number of dynamic cached shapes.

	\return The max number of cached dynamics.
	*/
	virtual PxU32 getMaxNbDynamicShapes() = 0;

	/**
	\brief Raycast against objects in the cache, returning results via PxRaycastCallback callback or PxRaycastBuffer object
	or a custom user callback implementation.
	
	Returns whether any rigid actor is hit along the ray.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in user guide article SceneQuery. User can ignore such objects by employing one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[out] hitCall		Raycast hit callback or hit buffer object.
	\param[in] hitFlags		Specifies which properties per hit should be computed and returned via the hit callback.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxQueryFlag flags are set. If NULL, all hits are assumed to be blocking.
	\return True if a blocking hit was found or any hit was found in case PxQueryFlag::eANY_HIT flag was used.

	@see PxRaycastCallback PxRaycastBuffer PxQueryFilterData PxQueryFilterCallback PxQueryCache PxRaycastHit PxQueryFlag PxQueryFlag::eANY_HIT
	*/
	virtual bool raycast(
		const PxVec3& origin, const PxVec3& unitDir, const PxReal distance,
		PxRaycastCallback& hitCall, PxHitFlags hitFlags = PxHitFlags(PxHitFlag::eDEFAULT),
		const PxQueryFilterData& filterData = PxQueryFilterData(), PxQueryFilterCallback* filterCall = NULL) const = 0;

	/**
	\brief Sweep against objects in the cache, returning results via PxRaycastCallback callback or PxRaycastBuffer object
	or a custom user callback implementation.
	
	Returns whether any rigid actor is hit along the sweep path.

	\param[in] geometry		Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose			Pose of the sweep object.
	\param[in] unitDir		Normalized direction of the sweep.
	\param[in] distance		Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[out] hitCall		Sweep hit callback or hit buffer object.
	\param[in] hitFlags		Specifies which properties per hit should be computed and returned via the hit callback.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxQueryFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] inflation	This parameter creates a skin around the swept geometry which increases its extents for sweeping. The sweep will register a hit as soon as the skin touches a shape, and will return the corresponding distance and normal.
	\return True if a blocking hit was found or any hit was found in case PxQueryFlag::eANY_HIT flag was specified.

	@see PxSweepCallback PxSweepBuffer PxQueryFilterData PxQueryFilterCallback PxSweepHit PxQueryCache PxHitFlags
	*/
	virtual bool sweep(
		const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
		PxSweepCallback& hitCall, PxHitFlags hitFlags = PxHitFlags(PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eDISTANCE),
		const PxQueryFilterData& filterData = PxQueryFilterData(), PxQueryFilterCallback* filterCall = NULL,
		const PxReal inflation = 0.f) const = 0;


	/**
	\brief Test overlap between a geometry and objects in the cache.
	
	\note Filtering: Overlap tests do not distinguish between touching and blocking hit types (see #PxQueryHitType). Both get written to the hit buffer.

	\param[in] geometry			Geometry of object to check for overlap (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the object.
	\param[out] hitCall			Overlap hit callback or hit buffer object.
	\param[in] filterData		Filtering data and simple logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxQueryFlag flags are set. If NULL, all hits are assumed to overlap.
	\return True if a blocking hit was found or any hit was found in case PxQueryFlag::eANY_HIT flag was specified.

	@see PxOverlapCallback PxOverlapBuffer PxQueryFilterData PxQueryFilterCallback
	*/
	virtual bool overlap(
		const PxGeometry& geometry, const PxTransform& pose, PxOverlapCallback& hitCall,
		const PxQueryFilterData& filterData = PxQueryFilterData(), PxQueryFilterCallback* filterCall = NULL) const = 0;

protected:
	virtual ~PxVolumeCache() {}
}; // class PxVolumeCache

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

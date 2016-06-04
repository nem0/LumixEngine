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


#ifndef PX_PHYSICS_SPATIAL_INDEX
#define PX_PHYSICS_SPATIAL_INDEX
/** \addtogroup physics
@{ */

#include "PxPhysXConfig.h"
#include "foundation/PxTransform.h"
#include "geometry/PxGeometry.h"
#include "PxQueryReport.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

typedef PxU32 PxSpatialIndexItemId;
static const PxSpatialIndexItemId PX_SPATIAL_INDEX_INVALID_ITEM_ID = 0xffffffff;


class PxSpatialIndexItem
{
};

/**
\brief Callback class for overlap queries against PxSpatialIndex

@see PxSpatialIndex
*/
struct PxSpatialOverlapCallback
{

	/**
	\brief callback method invoked when an overlap query hits an item in a PxSpatialIndex structure.

	\param[in] item the item that was hit.
	\return true if the query should continue, false if it should stop
	*/

	virtual PxAgain onHit(PxSpatialIndexItem& item) = 0;

	virtual ~PxSpatialOverlapCallback() {}
};

/**
\brief Callback class for raycast and sweep queries against PxSpatialIndex

@see PxSpatialIndex
*/
struct PxSpatialLocationCallback
{
	/**
	\brief callback method invoked when a sweep or raycast query hits an item in a PxSpatialIndex structure.

	\param[in]  item the item that was hit.
	\param[in]  distance the current maximum distance of the query.
	\param[out] shrunkDistance the updated maximum distance of the query. This must be no more than the current maximum distance.

	\return true if the query should continue, false if it should stop

	@see PxAgain
	*/
	virtual PxAgain onHit(PxSpatialIndexItem& item, PxReal distance, PxReal& shrunkDistance) = 0;

	virtual ~PxSpatialLocationCallback() {}
};



/**
\brief provides direct access to PhysX' Spatial Query engine

This class allows bounding boxes to be inserted, and then queried using sweep, raycast and overlap
checks. 

It is not thread-safe and defers handling some updates until queries are invoked, so care must be taken when calling any methods in parallel. Specifically,
to call query methods (raycast, overlap, sweep) in parallel, first call flush() to force immediate update of internal structures.

@see PxCreateSpatialIndex
*/
class PxSpatialIndex
{
public:


	/**
	\brief insert a bounding box into a spatial index

	\param[in] item the item to be inserted
	\param[in] bounds the bounds of the new item
	*/
	virtual	PxSpatialIndexItemId	insert(PxSpatialIndexItem& item,
										   const PxBounds3& bounds)							= 0;

	/**
	\brief update a bounding box in a spatial index

	\param[in] id the id of the item to be updated
	\param[in] bounds the new bounds of the item
	*/
	virtual	void					update(PxSpatialIndexItemId id,
										   const PxBounds3& bounds)							= 0;

	/**
	\brief remove an item from a spatial index

	\param[in] id the id of the item to be removed
	*/
	virtual	void					remove(PxSpatialIndexItemId id)							= 0;


	/**
	\brief make an overlap query against a spatial index

	\param[in] aabb the axis aligned bounding box for the query
	\param[in] callback the callback to invoke when an overlap is found
	*/
	virtual void					overlap(const PxBounds3 &aabb,
											PxSpatialOverlapCallback& callback)	const	= 0;

	/**
	\brief make a raycast query against a spatial index

	\param[in] origin the origin of the ray
	\param[in] unitDir a unit vector in the direction of the ray
	\param[in] maxDist the maximum distance to cast the ray
	\param[in] callback the callback to invoke when an item is hit by the ray
	*/
	virtual void					raycast(const PxVec3& origin, 
											const PxVec3& unitDir, 
											PxReal maxDist, 
											PxSpatialLocationCallback& callback)	const	= 0;

	/**
	\brief make a sweep query against a spatial index

	\param[in] aabb the axis aligned bounding box to sweep
	\param[in] unitDir a unit vector in the direction of the sweep
	\param[in] maxDist the maximum distance to apply the sweep
	\param[in] callback the callback to invoke when an item is hit by the sweep
	*/
	virtual	void					sweep(const PxBounds3& aabb, 
										  const PxVec3& unitDir, 
										  PxReal maxDist, 
										  PxSpatialLocationCallback& callback)		const	= 0;

	/**
	\brief force an immediate update of the internal structures of the index

	For reasons of efficiency an index structure may be lazily updated at the point of query if this method is not called. Once this method
	has been called, subsequent queries (sweeps, overlaps, raycasts) may be executed in parallel until the next write call to the index (insertion,
	removal, update, rebuild)
	*/
	virtual void					flush()													= 0;

	/**
	\brief force a full optimized rebuild of the index. 
	*/
	virtual void					rebuildFull()											= 0;

	/**
	\brief set the incremental rebuild rate for the index. 
	
	The index builds gradually in the background each time a rebuild step is taken; this value determines the number of steps required to rebuild the index.
	
	See PxScene::setDynamicTreeRebuildRateHint() for more information.

	\param[in] rate the rebuild rate

	@see PxScene::setDynamicTreeRebuildRateHint()
	*/
	virtual void					setIncrementalRebuildRate(PxU32 rate)					= 0;

	/**
	\brief take one step in rebuilding the tree. See setIncrementalRebuildRate()
	*/
	virtual void					rebuildStep()											= 0;

	/**
	\brief release this object
	*/
	virtual void					release()												= 0;
protected:
	virtual							~PxSpatialIndex(){}
};

/**
\brief Creates a spatial index.

@see PxSpatialIndex
*/
PX_PHYSX_CORE_API PxSpatialIndex* PxCreateSpatialIndex();

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

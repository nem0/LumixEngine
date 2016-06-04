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


#ifndef PX_PHYSICS_BROAD_PHASE_H
#define PX_PHYSICS_BROAD_PHASE_H
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxBounds3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxActor;

	/**
	\brief Broad phase algorithm used in the simulation

	eSAP is a good generic choice with great performance when many objects are sleeping. Performance
	can degrade significantly though, when all objects are moving, or when large numbers of objects
	are added to or removed from the broad phase. This algorithm does not need world bounds to be
	defined in order to work.

	eMBP is an alternative broad phase algorithm that does not suffer from the same performance
	issues as eSAP when all objects are moving or when inserting large numbers of objects. However
	its generic performance when many objects are sleeping might be inferior to eSAP, and it requires
	users to define world bounds in order to work.
	*/
	struct PxBroadPhaseType
	{
		enum Enum
		{
			eSAP,		//!< 3-axes sweep-and-prune
			eMBP,		//!< Multi box pruning

			eLAST
		};
	};

	/**
	\brief Broad-phase callback to receive broad-phase related events.

	Each broadphase callback object is associated with a PxClientID. It is possible to register different
	callbacks for different clients. The callback functions are called this way:
	- for shapes/actors, the callback assigned to the actors' clients are used
	- for aggregates, the callbacks assigned to clients from aggregated actors  are used

	\note SDK state should not be modified from within the callbacks. In particular objects should not
	be created or destroyed. If state modification is needed then the changes should be stored to a buffer
	and performed after the simulation step.

	<b>Threading:</b> It is not necessary to make this class thread safe as it will only be called in the context of the
	user thread.

	@see PxSceneDesc PxScene.setBroadPhaseCallback() PxScene.getBroadPhaseCallback()
	*/
	class PxBroadPhaseCallback
	{
		public:
		virtual				~PxBroadPhaseCallback()	{}

		/**
		\brief Out-of-bounds notification.
		
		This function is called when an object leaves the broad-phase.

		\param[in] shape	Shape that left the broad-phase bounds
		\param[in] actor	Owner actor
		*/
		virtual		void	onObjectOutOfBounds(PxShape& shape, PxActor& actor) = 0;

		/**
		\brief Out-of-bounds notification.
		
		This function is called when an aggregate leaves the broad-phase.

		\param[in] aggregate	Aggregate that left the broad-phase bounds
		*/
		virtual		void	onObjectOutOfBounds(PxAggregate& aggregate) = 0;
	};

	/**
	\brief "Region of interest" for the broad-phase.

	This is currently only used for the PxBroadPhaseType::eMBP broad-phase, which requires zones or regions to be defined
	when the simulation starts in order to work. Regions can overlap and be added or removed at runtime, but at least one
	region needs to be defined when the scene is created.

	If objects that do no overlap any region are inserted into the scene, they will not be added to the broad-phase and
	thus collisions will be disabled for them. A PxBroadPhaseCallback out-of-bounds notification will be sent for each one
	of those objects.

	The total number of regions is limited by PxBroadPhaseCaps::maxNbRegions.

	The number of regions has a direct impact on performance and memory usage, so it is recommended to experiment with
	various settings to find the best combination for your game. A good default setup is to start with global bounds
	around the whole world, and subdivide these bounds into 4*4 regions. The PxBroadPhaseExt::createRegionsFromWorldBounds
	function can do that for you.

	@see PxBroadPhaseCallback PxBroadPhaseExt.createRegionsFromWorldBounds
	*/
	struct PxBroadPhaseRegion
	{
		PxBounds3		bounds;		//!< Region's bounds
		void*			userData;	//!< Region's user-provided data
	};

	/**
	\brief Information & stats structure for a region
	*/
	struct PxBroadPhaseRegionInfo
	{
		PxBroadPhaseRegion	region;				//!< User-provided region data
		PxU32				nbStaticObjects;	//!< Number of static objects in the region
		PxU32				nbDynamicObjects;	//!< Number of dynamic objects in the region
		bool				active;				//!< True if region is currently used, i.e. it has not been removed
		bool				overlap;			//!< True if region overlaps other regions (regions that are just touching are not considering overlapping)
	};

	/**
	\brief Caps class for broad phase.
	*/
	struct PxBroadPhaseCaps
	{
		PxU32	maxNbRegions;			//!< Max number of regions supported by the broad-phase
		PxU32	maxNbObjects;			//!< Max number of objects supported by the broad-phase
		bool	needsPredefinedBounds;	//!< If true, broad-phase needs 'regions' to work
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_NX_SCENEDESC
#define PX_PHYSICS_NX_SCENEDESC
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxFlags.h"
#include "foundation/PxBounds3.h"
#include "PxFiltering.h"
#include "PxBroadPhase.h"
#include "common/PxTolerancesScale.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxCpuDispatcher;
class PxGpuDispatcher;
class PxSpuDispatcher;

/**
\brief Pruning structure used to accelerate scene queries.

eNONE uses a simple data structure that consumes less memory than the alternatives,
but generally has slower query performance.

eDYNAMIC_AABB_TREE usually provides the fastest queries. However there is a
constant per-frame management cost associated with this structure. How much work should
be done per frame can be tuned via the #PxSceneDesc::dynamicTreeRebuildRateHint
parameter.

eSTATIC_AABB_TREE is typically used for static objects. It is the same as the
dynamic AABB tree, without the per-frame overhead. This can be a good choice for static
objects, if no static objects are added, moved or removed after the scene has been
created. If there is no such guarantee (e.g. when streaming parts of the world in and out),
then the dynamic version is a better choice even for static objects.
*/
struct PxPruningStructure
{
	enum Enum
	{
		eNONE,					//!< Using a simple data structure
		eDYNAMIC_AABB_TREE,		//!< Using a dynamic AABB tree
		eSTATIC_AABB_TREE,		//!< Using a static AABB tree

		eLAST
	};
};


/**
\brief The order in which collide and solve are run in a normal simulation time-step

eCOLLIDE_SOLVE Indicates that collide is performed before solve
eSOLVE_COLLIDE Indicates that solve is performed before collide <b>(This feature is currently disabled)</b>

*/

struct PxSimulationOrder
{
	enum Enum
	{
		eCOLLIDE_SOLVE,		//!< Perform collide before solve
		eSOLVE_COLLIDE		//!< Perform solve before collide
	};
};



/**
\brief Enum for selecting the friction algorithm used for simulation.

#PxFrictionType::ePATCH selects the patch friction model which typically leads to the most stable results at low solver iteration counts and is also quite inexpensive, as it uses only
up to four scalar solver constraints per pair of touching objects.  The patch friction model is the same basic strong friction algorithm as PhysX 3.2 and before.  

#PxFrictionType::eONE_DIRECTIONAL is a simplification of the Coulomb friction model, in which the friction for a given point of contact is applied in the alternating tangent directions of
the contact's normal.  This simplification allows us to reduce the number of iterations required for convergence but is not as accurate as the two directional model.

#PxFrictionType::eTWO_DIRECTIONAL is identical to the one directional model, but it applies friction in both tangent directions simultaneously.  This hurts convergence a bit so it 
requires more solver iterations, but is more accurate.  Like the one directional model, it is applied at every contact point, which makes it potentially more expensive
than patch friction for scenarios with many contact points.
*/
	struct PxFrictionType
	{
		enum Enum
		{
			ePATCH,				//!< Select default patch-friction model.
			eONE_DIRECTIONAL,	//!< Select one directional per-contact friction model.
			eTWO_DIRECTIONAL	//!< Select two directional per-contact friction model.
		};
	};

/**
\brief flags for configuring properties of the scene

@see PxScene
*/

struct PxSceneFlag
{
	enum Enum
	{
		/**
		\brief Enable Active Transform Notification.

		This flag enables the the Active Transform Notification feature for a scene.  This
		feature defaults to disabled.  When disabled, the function
		PxScene::getActiveTransforms() will always return a NULL list.

		\note There may be a performance penalty for enabling the Active Transform Notification, hence this flag should
		only be enabled if the application intends to use the feature.

		<b>Default:</b> False
		*/
		eENABLE_ACTIVETRANSFORMS	=(1<<1),

		/**
		\brief Enables a second broad phase check after integration that makes it possible to prevent objects from tunneling through eachother.

		PxPairFlag::eDETECT_CCD_CONTACT requires this flag to be specified.

		\note For this feature to be effective for bodies that can move at a significant velocity, the user should raise the flag PxRigidBodyFlag::eENABLE_CCD for them.
		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> False

		@see PxRigidBodyFlag::eENABLE_CCD, PxPairFlag::eDETECT_CCD_CONTACT, eDISABLE_CCD_RESWEEP
		*/
		eENABLE_CCD	=(1<<2),

		/**
		\brief Enables a simplified swept integration strategy, which sacrifices some accuracy for improved performance.

		This simplified swept integration approach makes certain assumptions about the motion of objects that are not made when using a full swept integration. 
		These assumptions usually hold but there are cases where they could result in incorrect behavior between a set of fast-moving rigid bodies. A key issue is that
		fast-moving dynamic objects may tunnel through each-other after a rebound. This will not happen if this mode is disabled. However, this approach will be potentially 
		faster than a full swept integration because it will perform significantly fewer sweeps in non-trivial scenes involving many fast-moving objects. This approach 
		should successfully resist objects passing through the static environment.

		PxPairFlag::eDETECT_CCD_CONTACT requires this flag to be specified.

		\note This scene flag requires eENABLE_CCD to be enabled as well. If it is not, this scene flag will do nothing.
		\note For this feature to be effective for bodies that can move at a significant velocity, the user should raise the flag PxRigidBodyFlag::eENABLE_CCD for them.
		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> False

		@see PxRigidBodyFlag::eENABLE_CCD, PxPairFlag::eDETECT_CCD_CONTACT, eENABLE_CCD
		*/
		eDISABLE_CCD_RESWEEP	=(1<<3),


		/**
		\brief Enable adaptive forces to accelerate convergence of the solver. 
		
		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eADAPTIVE_FORCE				=(1<<4),


		/**
		\brief Enable contact pair filtering between kinematic and static rigid bodies.
		
		By default contacts between kinematic and static rigid bodies are suppressed (see #PxFilterFlag::eSUPPRESS) and don't get reported to the filter mechanism.
		Raise this flag if these pairs should go through the filtering pipeline nonetheless.

		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_KINEMATIC_STATIC_PAIRS =(1<<5),


		/**
		\brief Enable contact pair filtering between kinematic rigid bodies.
		
		By default contacts between kinematic bodies are suppressed (see #PxFilterFlag::eSUPPRESS) and don't get reported to the filter mechanism.
		Raise this flag if these pairs should go through the filtering pipeline nonetheless.

		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_KINEMATIC_PAIRS =(1<<6),


		/**
		\brief Enable GJK-based distance collision detection system.
		
		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_PCM	= (1 << 9),

		/**
		\brief Disable contact report buffer resize. Once the contact buffer is full, the rest of the contact reports will 
		not be buffered and sent.

		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.
		
		<b>Default:</b> false
		*/
		eDISABLE_CONTACT_REPORT_BUFFER_RESIZE	= (1 << 10),

		/**
		\brief Disable contact cache.
		
		Contact caches are used internally to provide faster contact generation. You can disable all contact caches
		if memory usage for this feature becomes too high.

		<b>Default:</b> false
		*/
		eDISABLE_CONTACT_CACHE	= (1 << 11),


		/**
		\brief Require scene-level locking

		When set to true this requires that threads accessing the PxScene use the
		multi-threaded lock methods.
		
		\note This flag is not mutable, and must be set in PxSceneDesc at scene creation.

		@see PxScene::lockRead
		@see PxScene::unlockRead
		@see PxScene::lockWrite
		@see PxScene::unlockWrite
		
		<b>Default:</b> false
		*/
		eREQUIRE_RW_LOCK = (1 << 12),

		/**
		\brief Enables additional stabilization pass in solver

		When set to true, this enables additional stabilization processing to improve that stability of complex interactions between large numbers of bodies.

		Note that this flag is not mutable and must be set in PxSceneDesc at scene creation. Also, this is an experimental feature which does result in some loss of momentum.
		*/
		eENABLE_STABILIZATION = (1 << 14),

		/**
		\brief Enables average points in contact manifolds

		When set to true, this enables additional contacts to be generated per manifold to represent the average point in a manifold. This can stabilize stacking when only a small
		number of solver iterations is used.

		Note that this flag is not mutable and must be set in PxSceneDesc at scene creation.
		*/
		eENABLE_AVERAGE_POINT = (1 << 15)

	};
};

/**
\brief collection of set bits defined in PxSceneFlag.

@see PxSceneFlag
*/
typedef PxFlags<PxSceneFlag::Enum,PxU16> PxSceneFlags;
PX_FLAGS_OPERATORS(PxSceneFlag::Enum,PxU16)


class PxSimulationEventCallback;
class PxContactModifyCallback;
class PxCCDContactModifyCallback;
class PxSimulationFilterCallback;

/**
\brief Class used to retrieve limits(e.g. maximum number of bodies) for a scene. The limits
are used as a hint to the size of the scene, not as a hard limit (i.e. it will be possible
to create more objects than specified in the scene limits).

0 indicates no limit.
*/
class PxSceneLimits
{
public:
	PxU32					maxNbActors;			//!< Expected maximum number of actors
	PxU32					maxNbBodies;			//!< Expected maximum number of dynamic rigid bodies
	PxU32					maxNbStaticShapes;		//!< Expected maximum number of static shapes
	PxU32					maxNbDynamicShapes;		//!< Expected maximum number of dynamic shapes
	PxU32					maxNbAggregates;		//!< Expected maximum number of aggregates
	PxU32					maxNbConstraints;		//!< Expected maximum number of constraint shaders
	PxU32					maxNbRegions;			//!< Expected maximum number of broad-phase regions
	PxU32					maxNbObjectsPerRegion;	//!< Expected maximum number of objects in one broad-phase region

	/**
	\brief constructor sets to default 
	*/
	PX_INLINE PxSceneLimits();

	/**
	\brief (re)sets the structure to the default
	*/
	PX_INLINE void setToDefault();

	/**
	\brief Returns true if the descriptor is valid.
	\return true if the current settings are valid.
	*/
	PX_INLINE bool isValid() const;
};

PX_INLINE PxSceneLimits::PxSceneLimits()	//constructor sets to default
{
	maxNbActors			= 0;
	maxNbBodies			= 0;
	maxNbStaticShapes	= 0;
	maxNbDynamicShapes	= 0;
	maxNbAggregates		= 0;
	maxNbConstraints	= 0;
	maxNbRegions		= 0;
	maxNbObjectsPerRegion = 0;
}

PX_INLINE void PxSceneLimits::setToDefault()
{
	*this = PxSceneLimits();
}

PX_INLINE bool PxSceneLimits::isValid() const	
{
	if(maxNbRegions>256)	// max number of regions is currently limited
		return false;

	return true;
}

/**
\brief Descriptor class for scenes. See #PxScene.

This struct must be initialized with the same PxTolerancesScale values used to initialize PxPhysics.

@see PxScene PxPhysics.createScene PxTolerancesScale
*/
class PxSceneDesc
{
public:

	/**
	\brief Gravity vector.

	<b>Range:</b> force vector<br>
	<b>Default:</b> Zero

	@see PxScene.setGravity()

	When setting gravity, you should probably also set bounce threshold.
	*/
	PxVec3					gravity;

	/**
	\brief Possible notification callback.

	This callback will be associated with the client PX_DEFAULT_CLIENT.
	Please use PxScene::setSimulationEventCallback() to register callbacks for other clients.

	<b>Default:</b> NULL

	@see PxSimulationEventCallback PxScene.setSimulationEventCallback() PxScene.getSimulationEventCallback()
	*/
	PxSimulationEventCallback*	simulationEventCallback;

	/**
	\brief Possible asynchronous callback for contact modification.

	<b>Default:</b> NULL

	@see PxContactModifyCallback PxScene.setContactModifyCallback() PxScene.getContactModifyCallback()
	*/
	PxContactModifyCallback*	contactModifyCallback;

	/**
	\brief Possible asynchronous callback for contact modification.

	<b>Default:</b> NULL

	@see PxContactModifyCallback PxScene.setContactModifyCallback() PxScene.getContactModifyCallback()
	*/
	PxCCDContactModifyCallback*	ccdContactModifyCallback;

	/**
	\brief Shared global filter data which will get passed into the filter shader.

	\note The provided data will get copied to internal buffers and this copy will be used for filtering calls.

	<b>Default:</b> NULL

	@see PxSimulationFilterShader
	*/
	const void*				filterShaderData;

	/**
	\brief Size (in bytes) of the shared global filter data #filterShaderData.

	<b>Default:</b> 0

	@see PxSimulationFilterShader filterShaderData
	*/
	PxU32					filterShaderDataSize;

	/**
	\brief The custom filter shader to use for collision filtering.

	\note This parameter is compulsory. If you don't want to define your own filter shader you can
	use the default shader #PxDefaultSimulationFilterShader which can be found in the PhysX extensions 
	library.

	@see PxSimulationFilterShader
	*/
	PxSimulationFilterShader	filterShader;

	/**
	\brief A custom collision filter callback which can be used to implement more complex filtering operations which need
	access to the simulation state, for example.

	<b>Default:</b> NULL

	@see PxSimulationFilterCallback
	*/
	PxSimulationFilterCallback*	filterCallback;

	/**
	\brief Selects the broad-phase algorithm to use.

	<b>Default:</b> PxBroadPhaseType::eSAP

	@see PxBroadPhaseType
	*/
	PxBroadPhaseType::Enum		broadPhaseType;

	/**
	\brief Broad-phase callback

	This callback will be associated with the client PX_DEFAULT_CLIENT.
	Please use PxScene::setBroadPhaseCallback() to register callbacks for other clients.

	<b>Default:</b> NULL

	@see PxBroadPhaseCallback
	*/
	PxBroadPhaseCallback*		broadPhaseCallback;

	/**
	\brief Expected scene limits.

	@see PxSceneLimits
	*/
	PxSceneLimits				limits;

	/**
	\deprecated
	\brief A small margin value used for mesh collision detection.
	(convex/box vs height field or convex/box vs triangle mesh)

	\note If interested in distance-based collision, please see
	the PxSceneFlag::eENABLE_PCM to enable the GJK/EPA path.

	\note Will be removed in future releases.

	@see PxTolerancesScale
	<b>Default:</b> 0.01 * PxTolerancesScale::length
	*/
	PX_DEPRECATED PxReal		meshContactMargin;


	/**
	\brief Selects the friction algorithm to use for simulation.

	\note frictionType cannot be modified after the first call to any of PxScene::simulate, PxScene::solve and PxScene::collide

	@see PxFrictionType
	<b>Default:</b> PxFrictionType::ePATCH

	@see PxScene::setFrictionType, PxScene::getFrictionType
	*/
	PxFrictionType::Enum frictionType;

	/**
	\deprecated
	\brief The patch friction model uses this coefficient to determine if a friction anchor can persist between frames.

	A friction anchor is a point on a body where friction gets applied, similar to a contact point.  The simulation determines 
	new potential friction anchors every time step, and deletes them if over time the bodies that they are attached to slide apart 
	by more than this distance.  We believe the user does not need to modify this parameter from its default.  For this reason we are 
	planning to remove it in future releases.  If you have an application that is relying on modifying this parameter, please let us know.

	The alternative Coulomb friction model does not use this coefficient. 

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 0.025 * PxTolerancesScale::length
	*/
	PX_DEPRECATED  PxReal	contactCorrelationDistance;

	/**
	\brief A contact with a relative velocity below this will not bounce. A typical value for simulation.
	stability is about 0.2 * gravity.

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 0.2 * PxTolerancesScale::speed

	@see PxMaterial
	*/
	PxReal bounceThresholdVelocity; 

	/**
	\brief A threshold of contact separation distance used to decide if a contact point will experience friction forces.

	\note If the separation distance of a contact point is greater than the threshold then the contact point will not experience friction forces. 

	\note If the aggregated contact offset of a pair of shapes is large it might be desirable to neglect friction
	for contact points whose separation distance is sufficiently large that the shape surfaces are clearly separated. 
	
	\note This parameter can be used to tune the separation distance of contact points at which friction starts to have an effect.  

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 0.04 * PxTolerancesScale::length
	*/
	PxReal frictionOffsetThreshold;

	/**
	\brief Flags used to select scene options.

	@see PxSceneFlag PxSceneFlags
	*/
	PxSceneFlags			flags;

	/**
	\brief The CPU task dispatcher for the scene.

	See PxCpuDispatcher, PxScene::getCpuDispatcher
	*/
	PxCpuDispatcher*	cpuDispatcher;

	/**
	\brief The GPU task dispatcher for the scene.

	<b>Platform specific:</b> Applies to PC GPU only.

	See PxGpuDispatcher, PxScene::getGpuDispatcher
	*/
	PxGpuDispatcher* 	gpuDispatcher;

	/**
	\brief The SPU task dispatcher for the scene.

	<b>Platform specific:</b> Applies to PS3 only.

	See PxSpuDispatcher, PxScene::getSpuDispatcher
	*/
	PxSpuDispatcher*	spuDispatcher;

	/**
	\brief Defines the structure used to store static objects.

	\note Only PxPruningStructure::eSTATIC_AABB_TREE and PxPruningStructure::eDYNAMIC_AABB_TREE are allowed here.
	*/
	PxPruningStructure::Enum	staticStructure;

	/**
	\brief Defines the structure used to store dynamic objects.
	*/
	PxPruningStructure::Enum	dynamicStructure;

	/**
	\brief Hint for how much work should be done per simulation frame to rebuild the pruning structure.

	This parameter gives a hint on the distribution of the workload for rebuilding the dynamic AABB tree
	pruning structure #PxPruningStructure::eDYNAMIC_AABB_TREE. It specifies the desired number of simulation frames
	the rebuild process should take. Higher values will decrease the workload per frame but the pruning
	structure will get more and more outdated the longer the rebuild takes (which can make
	scene queries less efficient).

	\note Only used for #PxPruningStructure::eDYNAMIC_AABB_TREE pruning structure.

	\note This parameter gives only a hint. The rebuild process might still take more or less time depending on the
	number of objects involved.

	<b>Range:</b> [4, PX_MAX_U32)<br>
	<b>Default:</b> 100
	*/
	PxU32					dynamicTreeRebuildRateHint;

	/**
	\brief Will be copied to PxScene::userData.

	<b>Default:</b> NULL
	*/
	void*					userData;

	/**
	\brief Defines the number of actors required to spawn a separate rigid body solver island task chain.

	This parameter defines the minimum number of actors required to spawn a separate rigid body solver task chain. Setting a low value 
	will potentially cause more task chains to be generated. This may result in the overhead of spawning tasks can become a limiting performance factor. 
	Setting a high value will potentially cause fewer islands to be generated. This may reduce thread scaling (fewer task chains spawned) and may 
	detrimentally affect performance if some bodies in the scene have large solver iteration counts because all constraints in a given island are solved by the 
	maximum number of solver iterations requested by any body in the island.

	<b>Default:</b> 32

	<b>Platform specific:</b> Not applicable on PS3. All bodies are batched into one island.

	@see PxScene.setSolverBatchSize() PxScene.getSolverBatchSize()
	*/
	PxU32					solverBatchSize;

	/**
	\brief Setting to define the number of 16K blocks that will be initially reserved to store contact, friction, and contact cache data.
	This is the number of 16K memory blocks that will be automatically allocated from the user allocator when the scene is instantiated. Further 16k
	memory blocks may be allocated during the simulation up to maxNbContactDataBlocks.

	\note This value cannot be larger than maxNbContactDataBlocks because that defines the maximum number of 16k blocks that can be allocated by the SDK.

	<b>Default:</b> 0, or 256 on PS3

	<b>Range:</b> [0, PX_MAX_U32]<br>

	@see PxPhysics::createScene PxScene::setNbContactDataBlocks 
	*/
	PxU32					nbContactDataBlocks;

	/**
	\brief Setting to define the maximum number of 16K blocks that can be allocated to store contact, friction, and contact cache data.
	As the complexity of a scene increases, the SDK may require to allocate new 16k blocks in addition to the blocks it has already 
	allocated. This variable controls the maximum number of blocks that the SDK can allocate.

	In the case that the scene is sufficiently complex that all the permitted 16K blocks are used, contacts will be dropped and 
	a warning passed to the error stream.

	If a warning is reported to the error stream to indicate the number of 16K blocks is insufficient for the scene complexity 
	then the choices are either (i) re-tune the number of 16K data blocks until a number is found that is sufficient for the scene complexity,
	(ii) to simplify the scene or (iii) to opt to not increase the memory requirements of physx and accept some dropped contacts.
	
	<b>Default:</b> 65536, or 256 on PS3

	<b>Range:</b> [0, PX_MAX_U32]<br>

	@see nbContactDataBlocks PxScene::setNbContactDataBlocks 
	*/
	PxU32					maxNbContactDataBlocks;

	/**
	\brief Size of the contact report stream (in bytes).
	
	The contact report stream buffer is used during the simulation to store all the contact reports. 
	If the size is not sufficient, the buffer will grow by a factor of two.
	It is possible to disable the buffer growth by setting the flag PxSceneFlag::eDISABLE_CONTACT_REPORT_BUFFER_RESIZE.
	In that case the buffer will not grow but contact reports not stored in the buffer will not get sent in the contact report callbacks.

	<b>Default:</b> 8192

	<b>Range:</b> (0, PX_MAX_U32]<br>
	
	*/
	PxU32					contactReportStreamBufferSize;

	/**
	\brief Maximum number of CCD passes 

	The CCD performs multiple passes, where each pass every object advances to its time of first impact. This value defines how many passes the CCD system should perform.
	
	\note The CCD system is a multi-pass best-effort conservative advancement approach. After the defined number of passes has been completed, any remaining time is dropped.
	\note This defines the maximum number of passes the CCD can perform. It may perform fewer if additional passes are not necessary.

	<b>Default:</b> 1
	<b>Range:</b> [1, PX_MAX_U32]<br>
	*/
	PxU32					ccdMaxPasses;

	/**
	\brief The simulation order 
	PhysX supports 2 simulation update approaches. The default model - eCOLLIDE_SOLVE - performs collision detection before solver. The alternative model, 
	eSOLVE_COLLIDE <b>(This feature is currently disabled)</b>, performs solve before collision. This has the performance benefit of allowing the game to defer collision detection for the subsequent frame
	so that it can overlap with things like game logic, rendering etc. However, it has the disadvantage that it requires insertions, removals and teleports to be deferred
	between dispatching collision detection and solve, which can potentially cause 1 frame's delay in these operations.

	<b>Default:</b> eCOLLIDE_SOLVE
	*/
	PxSimulationOrder::Enum		simulationOrder;

	/**
	\brief The wake counter reset value

	Calling wakeUp() on objects which support sleeping will set their wake counter value to the specified reset value.

	<b>Range:</b> (0, PX_MAX_F32)<br>
	<b>Default:</b> 0.4 (which corresponds to 20 frames for a time step of 0.02)

	@see PxRigidDynamic::wakeUp() PxArticulation::wakeUp() PxCloth::wakeUp()
	*/
	PxReal					wakeCounterResetValue;


	/**
	\brief The bounds used to sanity check user-set positions of actors and articulation links

	These bounds are used to check the position values of rigid actors inserted into the scene, and positions set for rigid actors
	already within the scene.

	<b>Range:</b> any valid PxBounds3 <br> 
	<b>Default:</b> (-PX_MAX_BOUNDS_EXTENTS, PX_MAX_BOUNDS_EXTENTS) on each axis
	*/
	PxBounds3				sanityBounds;

private:
	/**
	\cond
	*/
	// For internal use only
	PxTolerancesScale		tolerancesScale;
	/**
	\endcond
	*/


public:
	/**
	\brief constructor sets to default.

	\param[in] scale scale values for the tolerances in the scene, these must be the same values passed into
	PxCreatePhysics(). The affected tolerances are meshContactMargin, contactCorrelationDistance, bounceThresholdVelocity
	and frictionOffsetThreshold.

	@see PxCreatePhysics() PxTolerancesScale meshContactMargin contactCorrelationDistance bounceThresholdVelocity frictionOffsetThreshold
	*/	
	PX_INLINE PxSceneDesc(const PxTolerancesScale& scale);

	/**
	\brief (re)sets the structure to the default.

	\param[in] scale scale values for the tolerances in the scene, these must be the same values passed into
	PxCreatePhysics(). The affected tolerances are meshContactMargin, contactCorrelationDistance, bounceThresholdVelocity
	and frictionOffsetThreshold.

	@see PxCreatePhysics() PxTolerancesScale meshContactMargin contactCorrelationDistance bounceThresholdVelocity frictionOffsetThreshold
	*/
	PX_INLINE void setToDefault(const PxTolerancesScale& scale);

	/**
	\brief Returns true if the descriptor is valid.
	\return true if the current settings are valid.
	*/
	PX_INLINE bool isValid() const;

	/**
	\cond
	*/
	// For internal use only
	PX_INLINE const PxTolerancesScale& getTolerancesScale() const { return tolerancesScale; }
	/**
	\endcond
	*/
};

PX_INLINE PxSceneDesc::PxSceneDesc(const PxTolerancesScale& scale):
	gravity								(PxVec3(0.0f)),
	simulationEventCallback				(NULL),
	contactModifyCallback				(NULL),
	ccdContactModifyCallback			(NULL),

	filterShaderData					(NULL),
	filterShaderDataSize				(0),
	filterShader						(NULL),
	filterCallback						(NULL),
	broadPhaseType						(PxBroadPhaseType::eSAP),
	broadPhaseCallback					(NULL),

	meshContactMargin					(0.01f * scale.length),
	frictionType						(PxFrictionType::ePATCH),
	contactCorrelationDistance			(0.025f * scale.length),
	bounceThresholdVelocity				(0.2f * scale.speed),
	frictionOffsetThreshold				(0.04f * scale.length),

	flags								(0),

	cpuDispatcher						(NULL),
	gpuDispatcher						(NULL),
	spuDispatcher						(NULL),

	staticStructure						(PxPruningStructure::eDYNAMIC_AABB_TREE),
	dynamicStructure					(PxPruningStructure::eDYNAMIC_AABB_TREE),
	dynamicTreeRebuildRateHint			(100),

	userData							(NULL),

	solverBatchSize						(32),

#ifdef PX_PS3
	nbContactDataBlocks					(256),
#else
	nbContactDataBlocks					(0),
#endif

	maxNbContactDataBlocks				(1<<16),
	contactReportStreamBufferSize		(8192),
	ccdMaxPasses						(1),
	simulationOrder						(PxSimulationOrder::eCOLLIDE_SOLVE),
	wakeCounterResetValue				(20.0f*0.02f),
	sanityBounds						(PxBounds3(PxVec3(-PX_MAX_BOUNDS_EXTENTS, -PX_MAX_BOUNDS_EXTENTS, -PX_MAX_BOUNDS_EXTENTS),
												   PxVec3(PX_MAX_BOUNDS_EXTENTS, PX_MAX_BOUNDS_EXTENTS, PX_MAX_BOUNDS_EXTENTS))),
	tolerancesScale						(scale)
{
}

PX_INLINE void PxSceneDesc::setToDefault(const PxTolerancesScale& scale)
{
	*this = PxSceneDesc(scale);
}

PX_INLINE bool PxSceneDesc::isValid() const
{
	if(filterShader == NULL)
		return false;

	if( ((filterShaderDataSize == 0) && (filterShaderData != NULL)) ||
		((filterShaderDataSize > 0) && (filterShaderData == NULL)) )
		return false;

	if(!limits.isValid())
		return false;

	if(staticStructure!=PxPruningStructure::eSTATIC_AABB_TREE && staticStructure!=PxPruningStructure::eDYNAMIC_AABB_TREE)
		return false;

	if(dynamicTreeRebuildRateHint < 4)
		return false;

	if(meshContactMargin < 0.0f)
		return false;
	if(contactCorrelationDistance < 0.0f)
		return false;
	if(bounceThresholdVelocity < 0.0f)
		return false;
	if(frictionOffsetThreshold < 0.f)
		return false;

	if(cpuDispatcher == NULL)
		return false;

	if(contactReportStreamBufferSize == 0)
		return false;

	if(maxNbContactDataBlocks < nbContactDataBlocks)
		return false;

	if (wakeCounterResetValue <= 0.0f)
		return false;

#if !PX_ENABLE_INVERTED_STEPPER_FEATURE
	if (simulationOrder == PxSimulationOrder::eSOLVE_COLLIDE)
		return false;
#endif

	//Adaptive force and stabilization are incompatible. You can only have one or the other
	if((flags & (PxSceneFlag::eADAPTIVE_FORCE | PxSceneFlag::eENABLE_STABILIZATION)) == (PxSceneFlag::eADAPTIVE_FORCE | PxSceneFlag::eENABLE_STABILIZATION))
		return false;

	if(!sanityBounds.isValid())
		return false;

	return true;
}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

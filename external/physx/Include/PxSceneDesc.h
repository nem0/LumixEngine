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


#ifndef PX_PHYSICS_NX_SCENEDESC
#define PX_PHYSICS_NX_SCENEDESC
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "foundation/PxFlags.h"
#include "foundation/PxBounds3.h"
#include "PxPhysics.h"
#include "PxFiltering.h"
#include "common/PxTolerancesScale.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	namespace pxtask
	{
		class CpuDispatcher;
		class GpuDispatcher;
		class SpuDispatcher;
	}

/**
\brief Pruning structure used to accelerate scene queries (raycast, sweep tests, etc)

eNONE can be used without defining extra parameters. It typically doesn't provide
fast scene queries, but on the other hand it doesn't consume much memory. It is useful when
you don't use the SDK's scene queries at all.

eDYNAMIC_AABB_TREE usually provides the fastest queries. However there is a
constant per-frame management cost associated with this structure. You have the option to
give a hint on how much work should be done per frame by setting the parameter
#PxSceneDesc::dynamicTreeRebuildRateHint.

eSTATIC_AABB_TREE is typically used for static objects. It is the same as the
dynamic AABB tree, without the per-frame overhead. This is the default choice for static
objects. However, if you are streaming parts of the world in and out, you may want to use
the dynamic version even for static objects.
*/
struct PxPruningStructure
{
	enum Enum
	{
		eNONE,					//!< No structure, using a linear list of objects
		eDYNAMIC_AABB_TREE,		//!< Using a dynamic AABB tree
		eSTATIC_AABB_TREE,		//!< Using a static AABB tree

		eLAST
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
		\brief Used to disable use of SSE in the solver.

		SSE is detected at runtime(on appropriate platforms) and used if present by default.

		However use of SSE can be disabled, even if present, using this flag.

		<b>Platform:</b>
		\li PC SW: Yes
		\li PS3  : N/A
		\li XB360: N/A
		*/
		eDISABLE_SSE	= (1<<0),

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

		PxPairFlag::eSWEPT_INTEGRATION_LINEAR requires this flag to be specified.

		\note For this feature to be effective for shapes that can move at a significant velocity, the user should raise the flag PxShapeFlag::eUSE_SWEPT_BOUNDS for them.

		@see PxShapeFlag::eUSE_SWEPT_BOUNDS, PxPairFlag::eSWEPT_INTEGRATION_LINEAR
		*/
		eENABLE_SWEPT_INTEGRATION	=(1<<2),


		/**
		\brief Enable adaptive forces to accelerate convergence of the solver. 
		
		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> true
		*/
		eADAPTIVE_FORCE				=(1<<3),


		/**
		\brief Enable contact pair filtering between kinematic and static rigid bodies.
		
		By default contacts between kinematic and static rigid bodies are suppressed (see #PxFilterFlag::eSUPPRESS) and don't get reported to the filter mechanism.
		Raise this flag if these pairs should go through the filtering pipeline nonetheless.

		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_KINEMATIC_STATIC_PAIRS =(1<<4),


		/**
		\brief Enable contact pair filtering between kinematic rigid bodies.
		
		By default contacts between kinematic bodies are suppressed (see #PxFilterFlag::eSUPPRESS) and don't get reported to the filter mechanism.
		Raise this flag if these pairs should go through the filtering pipeline nonetheless.

		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_KINEMATIC_PAIRS =(1<<5),

		
		/**
		\brief Enable one directional per-contact friction model.
		
		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_ONE_DIRECTIONAL_FRICTION = (1<<6),

		/**
		\brief Enable two directional per-contact friction model.
		
		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		<b>Default:</b> false
		*/
		eENABLE_TWO_DIRECTIONAL_FRICTION = (1<<7),

		/**
		\brief Enable GJK-based distance collision detection system.
		
		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.

		In order to use this system, we need to register the system in the PxCreatePhysics

		<b>Default:</b> false
		*/
		eENABLE_PCM	= (1 << 8),

		/**
		\brief Disable contact report buffer resize. Once the contact buffer is full, the rest of the contact reports will 
		not be buffered and sent.

		Note that this flag is not mutable, and must be set in PxSceneDesc at scene creation.
		
		<b>Default:</b> false
		*/
		eDISABLE_CONTACT_REPORT_BUFFER_RESIZE	= (1 << 9)
	};
};

/**
\brief collection of set bits defined in PxSceneFlag.

@see PxSceneFlag
*/
typedef PxFlags<PxSceneFlag::Enum,PxU16> PxSceneFlags;
PX_FLAGS_OPERATORS(PxSceneFlag::Enum,PxU16);


class PxSimulationEventCallback;
class PxContactModifyCallback;
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
	PxU32					maxNbConstraints;		//!< Expected maximum number of constraint shaders

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
	\return true if the current settings are valid (returns always true).
	*/
	PX_INLINE bool isValid() const;
};

PX_INLINE PxSceneLimits::PxSceneLimits()	//constructor sets to default
{
	maxNbActors			= 0;
	maxNbBodies			= 0;
	maxNbStaticShapes	= 0;
	maxNbDynamicShapes	= 0;
	maxNbConstraints	= 0;

}

PX_INLINE void PxSceneLimits::setToDefault()
{
	*this = PxSceneLimits();
}

PX_INLINE bool PxSceneLimits::isValid() const	
{
	return true;
}

/**
\brief Descriptor class for scenes. See #PxScene.

@see PxScene PxPhysics.createScene
*/
class PxSceneDesc
{
public:

	/**
	\brief Gravity vector

	<b>Range:</b> force vector<br>
	<b>Default:</b> Zero

	@see PxScene.setGravity()

	When setting gravity, you should probably also set bounce threshold.
	*/
	PxVec3					gravity;

	/**
	\brief Possible notification callback 

	This callback will be associated with the client PX_DEFAULT_CLIENT.
	Please use PxScene::setSimulationEventCallback() to register callbacks for other clients.

	<b>Default:</b> NULL

	@see PxSimulationEventCallback PxScene.setSimulationEventCallback() PxScene.getSimulationEventCallback()
	*/
	PxSimulationEventCallback*	simulationEventCallback;

	/**
	\brief Possible asynchronous callback for contact modification

	<b>Default:</b> NULL

	@see PxContactModifyCallback PxScene.setContactModifyCallback() PxScene.getContactModifyCallback()
	*/
	PxContactModifyCallback*	contactModifyCallback;

	/**
	\brief Shared global filter data which will get passed into the filter shader

	\note The provided data will get copied to internal buffers and this copy will be used for filtering calls.

	<b>Default:</b> NULL

	@see PxSimulationFilterShader
	*/
	const void*				filterShaderData;

	/**
	\brief Size (in bytes) of the shared global filter data #filterShaderData

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
	\brief Expected scene limits

	@see PxSceneLimits
	*/
	PxSceneLimits				limits;

	/**
	\brief A small epsilon value used for swept collision detection.

	@see PxTolerancesScale
	<b>Default:</b> 0.01 * lengthScale
	*/
	PxReal                      sweepEpsilonDistance;


	/**
	\brief Two contacts are considered to be identical if their distance is below this value.

	Making this too small may make contact generation unstable.

	<b>Range:</b> [0, inf)<br>
	<b>Default:</b> 0.025 * lengthScale
	*/
	PxReal						contactCorrelationDistance;


	/**
	\brief A contact with a relative velocity below this will not bounce. A typical value for simulation
	stability is about 0.2 * gravity.

	<b>Range:</b> [0, inf)<br>
	<b>Default:</b> 2

	@see PxMaterial
	*/
	PxReal bounceThresholdVelocity; 


	/**
	\brief Flags used to select scene options.

	<b>Platform:</b>
	\li PC SW: Yes
	\li PS3  : Yes
	\li XB360: Yes
	\li WII	 : Yes

	@see PxSceneFlag PxSceneFlags
	*/
	PxSceneFlags			flags;

	/**
	\brief The CPU task dispatcher for the scene

	<b>Platform:</b>
	\li PC SW: Yes
	\li PS3  : Yes
	\li XB360: Yes
	\li WII	 : Yes

	See pxtask::CpuDispatcher
	*/
	pxtask::CpuDispatcher*	cpuDispatcher;

	/**
	\brief The GPU task dispatcher for the scene

	<b>Platform:</b>
	\li PC GPU: Yes
	\li PC SW : Not applicable
	\li PS3   : Not applicable
	\li XB360 : Not applicable
	\li WII	  : Not applicable

	See pxtask::GpuDispatcher
	*/
	pxtask::GpuDispatcher* 	gpuDispatcher;

	/**
	\brief The SPU task dispatcher for the scene

	<b>Platform:</b>
	\li PC SW: Not applicable
	\li PS3  : Yes
	\li XB360: Not applicable
	\li WII	 : Not applicable

	See pxtask::SpuDispatcher
	*/
	pxtask::SpuDispatcher*	spuDispatcher;

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

	<b>Range:</b> [5, inf]<br>
	<b>Default:</b> 100
	*/
	PxU32					dynamicTreeRebuildRateHint;

	/**
	\brief Will be copied to PxScene::userData

	<b>Default:</b> NULL
	*/
	void*					userData;

	/**
	\brief Defines the number of actors required to spawn a separate rigid body solver thread.

	<b>Default:</b> 32

	<b>Platform:</b>
	\li PC SW: Yes
	\li PS3  : Not applicable
	\li XB360: Yes
	\li WII	 : Yes

	@see PxScene.setSolverBatchSize() PxScene.getSolverBatchSize()
	*/
	PxU32					solverBatchSize;

	/**
	\brief Setting to determine how fast an object has to translate to perform swept integration

	For a pair of objects for which swept integration is enabled (see PxPairFlag::eSWEPT_INTEGRATION_LINEAR)
	swept integration will still be skipped if for both objects the below formula 
	evaluates to false:

	isMovingFast = smallest < (linearVelocity.magnitude() * a  + angularVelocity.magnitude() * b * largest )*dt

	Where

	smallest = bounds.halfDimensions().smallestDimension()
	largest = bounds.halfDimensions().largestDimension()

	a = sweptIntegrationLinearSpeedFactor
	b = sweptIntegrationAngularSpeedFactor

	a and b default to two because an object must only move half it's size to be considered fast, and this accounts for it.

	<b>Default:</b> 2

	@see sweptIntegrationAngularSpeedFactor

	*/
	PxReal					sweptIntegrationLinearSpeedFactor;

	/**
	\brief Setting to determine how fast an object has to rotate to perform swept integration

	<b>Default:</b> 2

	@see sweptIntegrationLinearSpeedFactor
	*/
	PxReal					sweptIntegrationAngularSpeedFactor;


	/**
	\brief Setting to determine how many 16K blocks are initially reserved to store contact, friction, and contact cache data.
	Memory blocks, each 16K, will be automatically allocated from the user allocator when the scene is instantiated.
	The initial number of 16K allocations is controlled by nbContactDataBlocks. In the case that the scene is sufficiently 
	complex that all 16K blocks are used, contacts will be dropped and a warning passed to the error stream.  

	If a warning is reported to the error stream to indicate the number of 16K blocks is insufficient for the scene complexity 
	then the choices are either (i) re-tune the number of 16K data blocks until a number is found that is sufficient for the scene complexity
	or (ii) to opt to not increase the memory requirements of physx and accept some dropped contacts.

	<b>Default:</b> 0, or 256 on PS3

	<b>Range:</b> [0, inf)<br>

	@see PxPhysics::createScene PxScene::setNbContactDataBlocks 
	*/
	PxU32					nbContactDataBlocks;


	/**
	\brief Setting to determine how many 16K blocks are reserved to store contact, friction, and contact cache data.
	Memory blocks, each 16K, will be automatically allocated from the user allocator when the scene is instantiated.
	The maximum number of 16K allocations is controlled by maxNbContactDataBlocks.
	In the case that the scene is sufficiently complex that all 16K blocks are used, contacts will be dropped and 
	a warning passed to the error stream.  

	If a warning is reported to the error stream to indicate the number of 16K blocks is insufficient for the scene complexity 
	then the choices are either (i) re-tune the number of 16K data blocks until a number is found that is sufficient for the scene complexity
	or (ii) to opt to not increase the memory requirements of physx and accept some dropped contacts.
	
	<b>Default:</b> 65536, or 256 on PS3

	<b>Range:</b> [0, inf)<br>

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

	<b>Range:</b> (0, inf)<br>
	
	*/
	PxU32					contactReportStreamBufferSize;

	/**
	\brief constructor sets to default (no gravity, no ground plane, collision detection on).

	\param[in] scale scale values for the tolerances in the scene, specifically sweepEpsilonDistance, 
	contactCorrelationDistance and bounceThresholdVelocity. Typically these should be the values passed into
	PxCreatePhysics().

	@see PxCreatePhysics() PxTolerancesScale
	*/
	PX_INLINE PxSceneDesc(const PxTolerancesScale& scale);

	/**
	\brief (re)sets the structure to the default (no gravity, no ground plane, collision detection on).	

	\param[in] scale scale values for the tolerances in the scene, specifically sweepEpsilonDistance, 
	contactCorrelationDistance and bounceThresholdVelocity. Typically these should be the values passed into
	PxCreatePhysics().

	@see PxCreatePhysics() PxTolerancesScale
	*/

	PX_INLINE void setToDefault(const PxTolerancesScale& scale);

	/**
	\brief Returns true if the descriptor is valid.
	\return true if the current settings are valid (returns always true).
	*/
	PX_INLINE bool isValid() const;

};

PX_INLINE PxSceneDesc::PxSceneDesc(const PxTolerancesScale& scale):
	gravity(PxVec3(0)),
	simulationEventCallback(NULL),
	contactModifyCallback(NULL),

	filterShaderData(NULL),
	filterShaderDataSize(0),
	filterShader(NULL),
	filterCallback(NULL),

	sweepEpsilonDistance(0.01f * scale.length),
	contactCorrelationDistance(0.025f * scale.length),
	bounceThresholdVelocity(0.2f * scale.speed),

	flags(0),

	cpuDispatcher(NULL),
	gpuDispatcher(NULL),
	spuDispatcher(NULL),

	staticStructure(PxPruningStructure::eSTATIC_AABB_TREE),
	dynamicStructure(PxPruningStructure::eDYNAMIC_AABB_TREE),
	dynamicTreeRebuildRateHint(100),

	userData(NULL),

	solverBatchSize(32),

	sweptIntegrationLinearSpeedFactor(2),
	sweptIntegrationAngularSpeedFactor(2),

#ifdef PX_PS3
	nbContactDataBlocks(256),
#else
	nbContactDataBlocks(0),
#endif

	maxNbContactDataBlocks(1<<16),
	contactReportStreamBufferSize(8192)
{
}

PX_INLINE void PxSceneDesc::setToDefault(const PxTolerancesScale& scale)
{
	*this = PxSceneDesc(scale);
}

PX_INLINE bool PxIsPowerOfTwo(PxU32 n)	{ return ((n&(n-1))==0);	}

PX_INLINE bool PxSceneDesc::isValid() const
{
	if (filterShader == NULL)
		return false;

	if ( ((filterShaderDataSize == 0) && (filterShaderData != NULL)) ||
		((filterShaderDataSize > 0) && (filterShaderData == NULL)) )
		return false;

	if (!limits.isValid())
		return false;

	if(staticStructure!=PxPruningStructure::eSTATIC_AABB_TREE && staticStructure!=PxPruningStructure::eDYNAMIC_AABB_TREE)
		return false;

	if (dynamicTreeRebuildRateHint < 5)
		return false;
	if (sweptIntegrationLinearSpeedFactor < 0.0f)
		return false;
	if (sweptIntegrationAngularSpeedFactor < 0.0f)
		return false;

	if (sweepEpsilonDistance < 0.0f)
		return false;
	if (contactCorrelationDistance < 0.0f)
		return false;
	if (bounceThresholdVelocity < 0.0f)
		return false;

	if(cpuDispatcher == NULL)
		return false;

	if(contactReportStreamBufferSize == 0)
		return false;

	if(maxNbContactDataBlocks < nbContactDataBlocks)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_NX_SCENE
#define PX_PHYSICS_NX_SCENE
/** \addtogroup physics
@{
*/

#include "PxVisualizationParameter.h"
#include "PxSceneDesc.h"
#include "PxSimulationStatistics.h"
#include "PxSceneQueryReport.h"
#include "PxSceneQueryFiltering.h"
#include "PxClient.h"

#if PX_USE_PARTICLE_SYSTEM_API
#include "particles/PxParticleSystem.h"
#include "particles/PxParticleFluid.h"
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxRigidStatic;
class PxRigidDynamic;
class PxConstraint;
class PxMaterial;
class PxSimulationEventCallback;
class PxPhysics;
class PxBatchQueryDesc;
class PxBatchQuery;
class PxSweepCache;
class PxAggregate;
class PxRenderBuffer;

class PxSphereGeometry;
class PxBoxGeometry;
class PxCapsuleGeometry;

typedef PxU8 PxDominanceGroup;

namespace pxtask
{
	class BaseTask;
	class TaskManager;
}

/**
\brief Maximum sweep distance for scene sweeps. The distance parameter for sweep functions will be clamped to this value.
The reason for this is GJK support cannot be evaluated near infinity. A viable alternative can be a sweep followed by an infinite raycast.

@see PxScene
*/
#define PX_MAX_SWEEP_DISTANCE 1e8f
/**
\brief Data struct for use with Active Transform Notification.
Used with PxScene::getActiveTransforms().

@see PxScene
*/
struct PxActiveTransform
{
	PxActor*		actor;				//!< Affected actor
	void*			userData;			//!< User data of the actor
	PxTransform		actor2World;		//!< Actor-to-world transform of the actor
};

/**
\brief Expresses the dominance relationship of a constraint.
For the time being only three settings are permitted:

(1.0f, 1.0f), (0.0f, 1.0f), and (1.0f, 0.0f).

@see getDominanceGroup() PxDominanceGroup PxScene::setDominanceGroupPair()
*/	
struct PxConstraintDominance
{
	PxConstraintDominance(PxReal a, PxReal b) 
		: dominance0(a), dominance1(b) {}
	PxReal dominance0;
	PxReal dominance1;
};

/**
\brief Identifies each type of actor for retrieving actors from a scene.

\note #PxArticulationLink objects are not supported. Use the #PxArticulation object to retrieve all its links.

@see PxScene::getActors(), PxScene::getNbActors()
*/
struct PxActorTypeSelectionFlag
{
	enum Enum
	{
		/**
		\brief A static rigid body
		@see PxRigidStatic
		*/
		eRIGID_STATIC		= (1 << 0),

		/**
		\brief A dynamic rigid body
		@see PxRigidDynamic
		*/
		eRIGID_DYNAMIC		= (1 << 1),

#if PX_USE_PARTICLE_SYSTEM_API
		/**
		\brief A particle system
		@see PxParticleSystem
		*/
		ePARTICLE_SYSTEM	= (1 << 2),

		/**
		\brief A particle fluid
		@see PxParticleFluid
		*/
		ePARTICLE_FLUID		= (1 << 3),
#endif

#if PX_USE_CLOTH_API
		/**
		\brief A cloth
		@see PxCloth
		*/
		eCLOTH				= (1 << 5)
#endif
	};
};

/**
\brief Collection of set bits defined in PxActorTypeSelectionFlag.

@see PxActorTypeSelectionFlag
*/
typedef PxFlags<PxActorTypeSelectionFlag::Enum,PxU16> PxActorTypeSelectionFlags;
PX_FLAGS_OPERATORS(PxActorTypeSelectionFlag::Enum,PxU16);

/**
\brief Hit cache for scene queries.

If this is supplied to a scene query, the shape is checked first for intersection.
If intersection is found, the hit is assumed to be a blocking hit.

It is the user's responsibility to ensure that the shape is valid, so care must be taken when deleting shapes to invalidate cached
references.

The faceIndex field is an additional hint for a mesh or height field which is not currently used.

@see PxScene.raycastAny PxScene.raycastSingle PxScene.raycastMultiple
*/
struct PxSceneQueryCache
{
	/**
	\brief constructor sets to default 
	*/
	PX_INLINE PxSceneQueryCache() : shape(NULL), faceIndex(0xffffffff) {}

	/**
	\brief constructor to set properties
	*/
	PX_INLINE PxSceneQueryCache(PxShape* s, PxU32 findex) : shape(s), faceIndex(findex) {}

	PxShape*	shape;			//!< Shape to test for intersection first
	PxU32		faceIndex;		//!< Triangle index to test first - NOT CURRENTLY SUPPORTED
};

/** 
 \brief A scene is a collection of bodies, particle systems and constraints which can interact.

 The scene simulates the behavior of these objects over time. Several scenes may exist 
 at the same time, but each body, particle system or constraint is specific to a scene 
 -- they may not be shared.

 @see PxSceneDesc PxPhysics.createScene() release()
*/
class PxScene
{
	protected:
										PxScene(): userData(0)	{}
	virtual								~PxScene()	{}

	public:

/************************************************************************************************/
	
/** @name Basics
*/
//@{
	
	/**
	\brief Deletes the scene.

	Removes any actors,  particle systems, and constraint shaders from this scene
	(if the user hasn't already done so).

	Be sure	to not keep a reference to this object after calling release.
	Avoid release calls while the scene is simulating (in between simulate() and fetchResults() calls).
	
	@see PxPhysics.createScene() 
	*/
	virtual		void					release() = 0;


	/**
	\brief Saves the Scene descriptor.

	\param[out] desc The descriptor used to retrieve the state of the object.
	\return True on success.

	@see PxSceneDesc
	*/
	virtual		bool					saveToDesc(PxSceneDesc& desc)	const	= 0;

	/**
	\brief Sets a scene flag.  You can only set one flag at a time.

	Only the below flags are mutable.  Trying to change the others will
	result in an error:

	PxSceneFlag::eENABLE_SWEPT_INTEGRATION

	@see PxSceneFlag
	*/
	virtual		void					setFlag(PxSceneFlag::Enum flag, bool value) = 0;

	/**
	\brief Get the scene flags.

	\return The scene flags. See #PxSceneFlag

	@see PxSceneFlag
	*/
	virtual		PxSceneFlags			getFlags() const = 0;

	/**
	\brief Sets a constant gravity for the entire scene.

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] vec A new gravity vector(e.g. PxVec3(0.0f,-9.8f,0.0f) ) <b>Range:</b> force vector

	@see PxSceneDesc.gravity getGravity()
	*/
	virtual void						setGravity(const PxVec3& vec) = 0;

	/**
	\brief Retrieves the current gravity setting.

	\return The current gravity for the scene.

	@see setGravity() PxSceneDesc.gravity
	*/
	virtual PxVec3						getGravity() const = 0;
	
	/**
	\brief Call this method to retrieve the Physics SDK.

	\return The physics SDK this scene is associated with.

	@see PxPhysics
	*/
	virtual	PxPhysics&					getPhysics() = 0;

	/**
	\brief Retrieves the scene's internal timestamp, increased each time a simulation step is completed.

	\return scene timestamp
	*/
	virtual	PxU32						getTimestamp()	const	= 0;

	void*	userData;	//!< user can assign this to whatever, usually to create a 1:1 relationship with a user object.

//@}

/************************************************************************************************/

/** @name Simulation
*/
//@{

	/**
 	\brief Advances the simulation by an elapsedTime time.
	
	\note Large elapsedTime values can lead to instabilities. In such cases elapsedTime
	should be subdivided into smaller time intervals and simulate() should be called
	multiple times for each interval.

	Calls to simulate() should pair with calls to fetchResults():
 	Each fetchResults() invocation corresponds to exactly one simulate()
 	invocation; calling simulate() twice without an intervening fetchResults()
 	or fetchResults() twice without an intervening simulate() causes an error
 	condition.
 
 	scene->simulate();
 	...do some processing until physics is computed...
 	scene->fetchResults();
 	...now results of run may be retrieved.


	\param[in] elapsedTime Amount of time to advance simulation by. The parameter has to be larger than 0, else the resulting behavior will be undefined. <b>Range:</b> (0,inf)
	\param[in] completionTask if non-NULL, this task will have its refcount incremented in simulate(), then
	decremented when the scene is ready to have fetchResults called. So the task will not run until the
	application also calls removeReference().
	\param[in] scratchMemBlock a memory region for physx to use for temporary data during simulation. This block may be reused by the application
	after fetchResults returns. Must be aligned on a 16-byte boundary
	\param[in] scratchMemBlockSize the size of the scratch memory block. Must be a multiple of 16K.
	\param[in] controlSimulation if true, the scene controls its TaskManager simulation state.  Leave
    true unless the application is calling the TaskManager start/stopSimulation() methods itself.

	@see fetchResults() checkResults()
	*/
	virtual	void						simulate(PxReal elapsedTime, 
												 physx::pxtask::BaseTask* completionTask = NULL,
												 void* scratchMemBlock = 0,
												 PxU32 scratchMemBlockSize = 0,
												 bool controlSimulation = true)		= 0;
	
	/**
	\brief This checks to see if the simulation run has completed.

	This does not cause the data available for reading to be updated with the results of the simulation, it is simply a status check.
	The bool will allow it to either return immediately or block waiting for the condition to be met so that it can return true
	
	\param[in] block When set to true will block until the condition is met.
	\return True if the results are available.

	@see simulate() fetchResults()
	*/
	virtual	bool						checkResults(bool block = false)	= 0;

	/**
	This is the big brother to checkResults() it basically does the following:
	
	\code
	if ( checkResults(block) )
	{
		fire appropriate callbacks
		swap buffers
		return true
	}
	else
		return false

	\endcode

	\param[in] block When set to true will block until the condition is met.
	\param[out] errorState Used to retrieve hardware error codes. A non zero value indicates an error.
	\return True if the results have been fetched.

	@see simulate() checkResults()
	*/
	virtual	bool						fetchResults(bool block = false, PxU32* errorState = 0)	= 0;

	/**
	\brief Clear internal buffers and free memory.

	This method can be used to clear buffers and free internal memory without having to destroy the scene. Can be usefull if
	the physics data gets streamed in and a checkpoint with a clean state should be created.

	\note It is not allowed to call this method while the simulation is running. The call will fail.
	
	\param[in] sendPendingReports When set to true pending reports will be sent out before the buffers get cleaned up (for instance lost touch contact/trigger reports due to deleted objects).
	*/
	virtual	void						flush(bool sendPendingReports = false) = 0;	
	
//@}

/************************************************************************************************/

/** @name Threads and Memory
*/
//@{
	
	/**
	\brief Get the task manager associated with this scene

	\return the task manager associated with the scene
	*/
	virtual physx::pxtask::TaskManager*		getTaskManager() const = 0;

	
	/**
	\brief Sets the number of actors required to spawn a separate rigid body solver thread.

	\param[in] solverBatchSize Number of actors required to spawn a separate rigid body solver thread.

	<b>Platform:</b>
	\li PC SW: Yes
	\li PS3  : Not applicable
	\li XB360: Yes
	\li WII	 : Yes

	@see PxSceneDesc.solverBatchSize getSolverBatchSize()
	*/
	virtual	void						setSolverBatchSize(PxU32 solverBatchSize) = 0;

	/**
	\brief Retrieves the number of actors required to spawn a separate rigid body solver thread.

	\return Current number of actors required to spawn a separate rigid body solver thread.

	<b>Platform:</b>
	\li PC SW: Yes
	\li PS3  : Not applicable
	\li XB360: Yes
	\li WII	 : Yes

	@see PxSceneDesc.solverBatchSize setSolverBatchSize()
	*/
	virtual PxU32						getSolverBatchSize() const = 0;
	
	
	/**
	\brief set the cache blocks that can be used during simulate(). 
	
	Each frame the simulation requires memory to store contact, friction, and contact cache data. This memory is used in blocks of 16K.
	Each frame the blocks used by the previous frame are freed, and may be retrieved by the application using PxScene::flush()

	This call will force allocation of cache blocks if the numBlocks parameter is greater than the currently allocated number
	of blocks, and less than the max16KContactDataBlocks parameter specified at scene creation time.

	\param[in] numBlocks The number of blocks to allocate.	

	@see PxSceneDesc.nbContactDataBlocks PxSceneDesc.maxNbContactDataBlocks flush() getNbContactDataBlocksUsed getMaxNbContactDataBlocksUsed
	*/
	virtual         void				setNbContactDataBlocks(PxU32 numBlocks) = 0;
	

	/**
	\brief get the number of cache blocks currently used by the scene 

	This function may not be called while the scene is simulating

	\return the number of cache blocks currently used by the scene

	@see PxSceneDesc.nbContactDataBlocks PxSceneDesc.maxNbContactDataBlocks flush() setNbContactDataBlocks() getMaxNbContactDataBlocksUsed()
	*/
	virtual         PxU32				getNbContactDataBlocksUsed() const = 0;

	/**
	\brief get the maximum number of cache blocks used by the scene 

	This function may not be called while the scene is simulating

	\return the maximum number of cache blocks ever used by the scene

	@see PxSceneDesc.nbContactDataBlocks PxSceneDesc.maxNbContactDataBlocks flush() setNbContactDataBlocks() getNbContactDataBlocksUsed()
	*/
	virtual         PxU32				getMaxNbContactDataBlocksUsed() const = 0;


//@}

/************************************************************************************************/

/** @name Create/Release Objects
*/
//@{
	/**
	\brief Adds an articulation to this scene.

	\note If the articulation is already assigned to a scene (see #PxArticulation::getScene), the call is ignored and a error is issued.

	\param[in] articulation Articulation to add to scene. See #PxArticulation

	@see PxArticulation
	*/
	virtual	void						addArticulation(PxArticulation& articulation) = 0;

	/**
	\brief Removes an articulation from this scene.

	\note If the articulation is not part of this scene (see #PxArticulation::getScene), the call is ignored and a error is issued. 
	
	\param[in] articulation Articulation to remove from scene. See #PxArticulation

	@see PxArticulation
	*/
	virtual	void						removeArticulation(PxArticulation& articulation) = 0;

	/**
	\brief Adds an actor to this scene.
	
	\note If the actor is already assigned to a scene (see #PxActor::getScene), the call is ignored and a error is issued.

	\note You can not add individual articulation links (see #PxArticulationLink) to the scene. Use #addArticulation() instead.

	\note If the actor is a PxRigidActor then each assigned PxConstraint object will get added to the scene automatically if
	it connects to another actor that is part of the scene already.

	\param[in] actor Actor to add to scene.

	@see PxActor
	*/
	virtual	void						addActor(PxActor& actor) = 0;

	// PT: work in progress. Don't use yet.
	virtual	void						addActors(PxU32 nbActors, PxActor** actors) = 0;

	/**
	\brief Removes an actor from this scene.

	\note If the actor is not part of this scene (see #PxActor::getScene), the call is ignored and a error is issued.

	\note You can not remove individual articulation links (see #PxArticulationLink) from the scene. Use #removeArticulation() instead.

	\note If the actor is a PxRigidActor then all assigned PxConstraint objects will get removed from the scene automatically.

	\param[in] actor Actor to remove from scene.

	@see PxActor
	*/
	virtual	void						removeActor(PxActor& actor) = 0;


// PX_AGGREGATE
	/**
	\brief Adds an aggregate to this scene.
	
	\note If the aggregate is already assigned to a scene (see #PxAggregate::getScene), the call is ignored and a error is issued.

	\note If the aggregate already contains actors, those actors are added to the scene as well.

	\param[in] aggregate Aggregate to add to scene.
	
	@see PxAggregate
	*/
    virtual	void						addAggregate(PxAggregate& aggregate)	= 0;

	/**
	\brief Removes an aggregate from this scene.

	\note If the aggregate is not part of this scene (see #PxAggregate::getScene), the call is ignored and a error is issued.

	\note If the aggregate contains actors, those actors are removed from the scene as well.

	\param[in] aggregate Aggregate to remove from scene.

	@see PxAggregate
	*/
	virtual	void						removeAggregate(PxAggregate& aggregate)	= 0;

	/**
	\brief Returns the number of aggregates in the scene.

	\return the number of aggregates in this scene.

	@see getAggregates()
	*/
	virtual			PxU32				getNbAggregates()	const	= 0;

	/**
	\brief Retrieve all the aggregates in the scene.

	\param[out] userBuffer The buffer to receive aggregates pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first aggregate pointer to be retrieved
	\return Number of aggregates written to the buffer.

	@see getNbAggregates()
	*/
	virtual			PxU32				getAggregates(PxAggregate** userBuffer, PxU32 bufferSize, PxU32 startIndex=0)	const	= 0;
//~PX_AGGREGATE

//@}
/************************************************************************************************/

/** @name Dominance
*/
//@{

	/**
	\brief Specifies the dominance behavior of constraints between two actors with two certain dominance groups.
	
	It is possible to assign each actor to a dominance groups using #PxActor::setDominanceGroup().

	With dominance groups one can have all constraints (contacts and joints) created 
	between actors act in one direction only. This is useful if you want to make sure that the movement of the rider
	of a vehicle or the pony tail of a character doesn't influence the object it is attached to, while keeping the motion of 
	both inherently physical.  
	
	Whenever a constraint (i.e. joint or contact) between two actors (a0, a1) needs to be solved, the groups (g0, g1) of both
	actors are retrieved.  Then the PxConstraintDominance setting for this group pair is retrieved with getDominanceGroupPair(g0, g1).  
	
	In the constraint, PxConstraintDominance::dominance0 becomes the dominance setting for a0, and 
	PxConstraintDominance::dominance1 becomes the dominance setting for a1.  A dominanceN setting of 1.0f, the default, 
	will permit aN to be pushed or pulled by a(1-N) through the constraint.  A dominanceN setting of 0.0f, will however 
	prevent aN to be pushed or pulled by a(1-N) through the constraint.  Thus, a PxConstraintDominance of (1.0f, 0.0f) makes 
	the interaction one-way.
	
	
	The matrix sampled by getDominanceGroupPair(g1, g2) is initialised by default such that:
	
	if g1 == g2, then (1.0f, 1.0f) is returned
	if g1 <  g2, then (0.0f, 1.0f) is returned
	if g1 >  g2, then (1.0f, 0.0f) is returned
	
	In other words, we permit actors in higher groups to be pushed around by actors in lower groups by default.
		
	These settings should cover most applications, and in fact not overriding these settings may likely result in higher performance.
	
	It is not possible to make the matrix asymetric, or to change the diagonal.  In other words: 
	
	* it is not possible to change (g1, g2) if (g1==g2)	
	* if you set 
	
	(g1, g2) to X, then (g2, g1) will implicitly and automatically be set to ~X, where:
	
	~(1.0f, 1.0f) is (1.0f, 1.0f)
	~(0.0f, 1.0f) is (1.0f, 0.0f)
	~(1.0f, 0.0f) is (0.0f, 1.0f)
	
	These two restrictions are to make sure that constraints between two actors will always evaluate to the same dominance
	setting, regardless of which order the actors are passed to the constraint.
	
	Dominance settings are currently specified as floats 0.0f or 1.0f because in the future we may permit arbitrary 
	fractional settings to express 'partly-one-way' interactions.
		
	<b>Sleeping:</b> Does <b>NOT</b> wake actors up automatically.

	@see getDominanceGroupPair() PxDominanceGroup PxConstraintDominance PxActor::setDominanceGroup() PxActor::getDominanceGroup()
	*/
	virtual void 						setDominanceGroupPair(PxDominanceGroup group1, PxDominanceGroup group2, const PxConstraintDominance& dominance) = 0;

	/**
	\brief Samples the dominance matrix.

	@see setDominanceGroupPair() PxDominanceGroup PxConstraintDominance PxActor::setDominanceGroup() PxActor::getDominanceGroup()
	*/
	virtual PxConstraintDominance 		getDominanceGroupPair(PxDominanceGroup group1, PxDominanceGroup group2) const = 0;

//@}
/************************************************************************************************/

/** @name Enumeration
*/
//@{

	/**
	\brief Retrieve the number of actors of certain types in the scene.

	\param[in] types Combination of actor types.
	\return the number of actors.

	@see getActors()
	*/
	virtual	PxU32						getNbActors(PxActorTypeSelectionFlags types) const = 0;

	/**
	\brief Retrieve an array of all the actors of certain types in the scene.

	\param[in] types Combination of actor types to retrieve.
	\param[out] userBuffer The buffer to receive actor pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first actor pointer to be retrieved
	\return Number of actors written to the buffer.

	@see getNbActors()
	*/
	virtual	PxU32						getActors(PxActorTypeSelectionFlags types, PxActor** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const	= 0;

	/**
	\brief Queries the PxScene for a list of the PxActors whose transforms have been 
	updated during the previous simulation step

	Note: PxSceneFlag::eENABLE_ACTIVETRANSFORMS must be set.
	Multiclient behavior: Active transforms now return only the list of active actors owned by the specified client.

	\note Do not use this method while the simulation is running. Calls to this method while the simulation is running will be ignored and NULL will be returned.

	\param[out] nbTransformsOut The number of transforms returned.
	\param[in] client The client whose actors the caller is interested in.

	\return A pointer to the list of PxActiveTransforms generated during the last call to fetchResults().

	@see PxActiveTransform
	*/

	virtual PxActiveTransform*			getActiveTransforms(PxU32& nbTransformsOut, PxClientID client = PX_DEFAULT_CLIENT) = 0;

	/**
	\brief Returns the number of articulations in the scene.

	\return the number of articulations in this scene.

	@see getArticulations()
	*/
	virtual PxU32						getNbArticulations() const = 0;

	/**
	\brief Retrieve all the articulations in the scene.

	\param[out] userBuffer The buffer to receive articulations pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first articulations pointer to be retrieved
	\return Number of articulations written to the buffer.

	@see getNbArticulations()
	*/
	virtual	PxU32						getArticulations(PxArticulation** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;

	/**
	\brief Returns the number of constraint shaders in the scene.

	\return the number of constraint shaders in this scene.

	@see getConstraints()
	*/
	virtual PxU32						getNbConstraints()	const	= 0;

	/**
	\brief Retrieve all the constraint shaders in the scene.

	\param[out] userBuffer The buffer to receive constraint shader pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first constraint pointer to be retrieved
	\return Number of constraint shaders written to the buffer.

	@see getNbConstraints()
	*/
	virtual	PxU32						getConstraints(PxConstraint** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;


//@}
/************************************************************************************************/

/** @name Multiclient
*/
//@{
	/**
	\brief Reserves a new client ID.  
	
	PX_DEFAULT_CLIENT is always available as the default clientID.  
	Additional clients are returned by this function.  Clients cannot be released once created. 
	An error is reported when more than a supported number of clients (currently 128) are created. 
	*/
	virtual PxClientID 					createClient() = 0;

	/**
	\brief Sets behavior bits for a client.

	The behavior bits are a property of a client that determine when it receives callbacks.
	PxU32 is a combo of PxClientBehaviorBit.  

	It is permissible to change the behavior for PX_DEFAULT_CLIENT with this call.
	Initially all created clients, as well as PX_DEFAULT_CLIENT have all bits set to 0.

	Note that in addition to setting a client to listen to a particular foreign actor event type, 
	the user must also configure actors to send that particular event type to foreign clients
	using PxActor::setClientBehaviorBits().

	@see PxClientBehaviorBit PxClientID createClient() getClientBehaviorBits() PxActor::setClientBehaviorBits()
	*/
	virtual void 						setClientBehaviorBits(PxClientID client, PxU32 clientBehaviorBits) = 0; 

	/**
	\brief Retrieves behavior bits for a client.

	@see PxClientBehaviorBit PxClientID setClientBehaviorBits() createClient()
	*/
	virtual PxU32 						getClientBehaviorBits(PxClientID client) const = 0;
//@}
/************************************************************************************************/

/** @name Callbacks
*/
//@{

	/**
	\brief Sets a user notify object which receives special simulation events when they occur.

	Multiclient behavior: Unlike the PxSimulationEventCallback that can be specified in the PxSceneDesc, this method 
	lets the user associate additional callbacks with clients other than PX_DEFAULT_CLIENT.  This way 
	each client can register its own callback class.  Each callback function has a somewhat differnt
	way of determining which clients' callbacks will be called in a certain event.  Refer to the documentation
	of particular callback functions inside PxSimulationEventCallback for this information.

	\note Do not set the callback while the simulation is running. Calls to this method while the simulation is running will be ignored.

	\param[in] callback User notification callback. See #PxSimulationEventCallback.
	\param[in] client The client to be associated with this callback.

	@see PxSimulationEventCallback getSimulationEventCallback
	*/
	virtual void						setSimulationEventCallback(PxSimulationEventCallback* callback, PxClientID client = PX_DEFAULT_CLIENT) = 0;

	/**
	\brief Retrieves the simulationEventCallback pointer set with setSimulationEventCallback().

	\param[in] client The client whose callback object is to be returned.
	\return The current user notify pointer. See #PxSimulationEventCallback.

	@see PxSimulationEventCallback setSimulationEventCallback()
	*/
	virtual PxSimulationEventCallback*	getSimulationEventCallback(PxClientID client = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sets a user callback object, which receives callbacks on all contacts generated for specified actors.

	\note Do not set the callback while the simulation is running. Calls to this method while the simulation is running will be ignored.

	\param[in] callback Asynchronous user contact modification callback. See #PxContactModifyCallback.
	*/
	virtual void						setContactModifyCallback(PxContactModifyCallback* callback) = 0;

	/**
	\brief Retrieves the PxContactModifyCallback pointer set with setContactModifyCallback().

	\return The current user contact modify callback pointer. See #PxContactModifyCallback.

	@see PxContactModifyCallback setContactModifyCallback()
	*/
	virtual PxContactModifyCallback*	getContactModifyCallback() const = 0;

//@}
/************************************************************************************************/

/** @name Collision Filtering
*/
//@{

	/**
	\brief Gets the shared global filter data in use for this scene.

	\note The reference points to a copy of the original filter data specified in PxSceneDesc.filterShaderData.

	\return Shared filter data for filter shader.

	@see getFilterShaderDataSize() PxSceneDesc.filterShaderData PxSimulationFilterShader
	*/
	virtual	const void*					getFilterShaderData() const = 0;

	/**
	\brief Gets the size of the shared global filter data (#PxSceneDesc.filterShaderData)

	\return Size of shared filter data [bytes].

	@see getFilterShaderData() PxSceneDesc.filterShaderDataSize PxSimulationFilterShader
	*/
	virtual	PxU32						getFilterShaderDataSize() const = 0;

	/**
	\brief Gets the custom collision filter shader in use for this scene.

	\return Filter shader class that defines the collision pair filtering.

	@see PxSceneDesc.filterShader PxSimulationFilterShader
	*/
	virtual	PxSimulationFilterShader 	getFilterShader() const = 0;

	/**
	\brief Gets the custom collision filter callback in use for this scene.

	\return Filter callback class that defines the collision pair filtering.

	@see PxSceneDesc.filterCallback PxSimulationFilterCallback
	*/
	virtual	PxSimulationFilterCallback*	getFilterCallback() const = 0;

//@}

/************************************************************************************************/

/** @name Scene Query
*/
//@{


	/**
	\brief Creates a BatchQuery object. 

	Scene queries like raycasts, overlap tests and sweeps are batched in this object and are then executed at once. See #PxBatchQuery.

	\param[in] desc The descriptor of scene query. Scene Queries need to register a callback. See #PxBatchQueryDesc.

	@see PxBatchQuery PxBatchQueryDesc
	*/
	virtual	PxBatchQuery*				createBatchQuery(const PxBatchQueryDesc& desc) = 0;

	/**
	\brief Creates a sweep cache, for use with PxBatchQuery::linearCompoundGeometrySweepMultiple(). See the Guide, "Sweep API" section for more information.

	\param[in] dimensions the dimensions of the sweep cache. Objects within this distance of the swept volume will be cached

	@see PxSweepCache PxBatchQuery
	*/
	PX_DEPRECATED	virtual	PxSweepCache*				createSweepCache(PxReal dimensions = 5.0f) = 0;

	/**
	\brief Sets the rebuild rate of the dynamic tree pruning structure.

	\param[in] dynamicTreeRebuildRateHint Rebuild rate of the dynamic tree pruning structure.

	@see PxSceneDesc.dynamicTreeRebuildRateHint getDynamicTreeRebuildRateHint()
	*/
	virtual	void						setDynamicTreeRebuildRateHint(PxU32 dynamicTreeRebuildRateHint) = 0;

	/**
	\brief Retrieves the rebuild rate of the dynamic tree pruning structure.

	\return The rebuild rate of the dyamic tree pruning structure.

	@see PxSceneDesc.dynamicTreeRebuildRateHint setDynamicTreeRebuildRateHint()
	*/
	virtual PxU32						getDynamicTreeRebuildRateHint() const = 0;


	/**
	\brief Raycast returning any blocking hit, not necessarily the closest.
	
	Returns whether any rigid actor is hit along the ray.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[out] hit			Raycast hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first. If no hit is found the ray gets queried against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient	ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache PxSceneQueryHit
	*/
	virtual bool raycastAny(const PxVec3& origin, const PxVec3& unitDir, const PxReal distance,
							PxSceneQueryHit& hit,
							const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							PxSceneQueryFilterCallback* filterCall = NULL,
							const PxSceneQueryCache* cache = NULL,
							PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Raycast returning a single result.
	
	Returns the first rigid actor that is hit along the ray. Data for a blocking hit will be returned as specified by the outputFlags field. Touching hits will be ignored.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin		Origin of the ray.
	\param[in] unitDir		Normalized direction of the ray.
	\param[in] distance		Length of the ray. Needs to be larger than 0.
	\param[in] outputFlags	Specifies which properties should be written to the hit information
	\param[out] hit			Raycast hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache		Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient	ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFlags PxRaycastHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual bool raycastSingle(const PxVec3& origin, const PxVec3& unitDir, const PxReal distance,
							   PxSceneQueryFlags outputFlags,
							   PxRaycastHit& hit,
							   const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							   PxSceneQueryFilterCallback* filterCall = NULL,
							   const PxSceneQueryCache* cache = NULL,
							   PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Raycast returning multiple results.
	
	Find all rigid actors that get hit along the ray. Each result contains data as specified by the outputFlags field.

	\note Touching hits are not ordered.

	\note Shooting a ray from within an object leads to different results depending on the shape type. Please check the details in article SceneQuery. User can ignore such objects by using one of the provided filter mechanisms.

	\param[in] origin			Origin of the ray.
	\param[in] unitDir			Normalized direction of the ray.
	\param[in] distance			Length of the ray. Needs to be larger than 0.
	\param[in] outputFlags		Specifies which properties should be written to the hit information
	\param[out] hitBuffer		Raycast hit information buffer. If the buffer overflows, the blocking hit is returned as the last entry together with an arbitrary subset
								of the nearer touching hits (typically the query should be restarted with a larger buffer).
	\param[in] hitBufferSize	Size of the hit buffer.
	\param[out] blockingHit		True if a blocking hit was found. If found, it is the last in the buffer, preceded by any touching hits which are closer. Otherwise the touching hits are listed.
	\param[in] filterData		Filtering data and simple logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be touching.
	\param[in] cache			Cached hit shape (optional). Ray is tested against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return Number of hits in the buffer, or -1 if the buffer overflowed.

	@see PxSceneQueryFlags PxRaycastHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual PxI32 raycastMultiple(const PxVec3& origin, const PxVec3& unitDir, const PxReal distance,
								  PxSceneQueryFlags outputFlags,
								  PxRaycastHit* hitBuffer,
								  PxU32 hitBufferSize,
								  bool& blockingHit,
								  const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
								  PxSceneQueryFilterCallback* filterCall = NULL,
								  const PxSceneQueryCache* cache = NULL,
								  PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;
	
	/**
	\brief Sweep returning any blocking hit, not necessarily the closest.
	
	Returns whether any rigid actor is hit along the sweep path.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometry		Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose			Pose of the sweep object.
	\param[in] unitDir		Normalized direction of the sweep.
	\param[in] distance		Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] queryFlags	Combination of PxSceneQueryFlag defining the query behavior
	\param[out] hit			Sweep hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache		Cached hit shape (optional). Sweep is performed against cached shape first. If no hit is found the sweep gets queried against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient	ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryHit PxSceneQueryCache
	*/
	virtual bool sweepAny(	const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
							PxSceneQueryFlags queryFlags,
							PxSceneQueryHit& hit,
							const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							PxSceneQueryFilterCallback* filterCall = NULL,
							const PxSceneQueryCache* cache = NULL,
							PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sweep of geometry compound returning any blocking hit, not necessarily the closest.
	
	The function sweeps all specified geometry objects through space and returns whether any rigid actor is hit along the sweep path.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometryList		Geometries of objects to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] poseList			The world pose for each geometry object.
	\param[in] filterDataList	Filter data for each geometry object. NULL, if no filtering should be done, all hits are assumed to be blocking in that case.
	\param[in] geometryCount	Number of geometry objects specified.
	\param[in] unitDir			Normalized direction of the sweep.
	\param[in] distance			Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] queryFlags		Combination of PxSceneQueryFlag defining the query behavior
	\param[out] hit				Sweep hit information.
	\param[in] filterFlags		Simple filter logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache			Cached hit shape (optional). Sweep is performed against cached shape first. If no hit is found the sweep gets queried against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryHit PxSceneQueryCache
	*/
	virtual bool sweepAny(	const PxGeometry** geometryList, const PxTransform* poseList, const PxFilterData* filterDataList, PxU32 geometryCount, 
							const PxVec3& unitDir, const PxReal distance,
							PxSceneQueryFlags queryFlags,
							PxSceneQueryHit& hit,
							PxSceneQueryFilterFlags filterFlags = PxSceneQueryFilterFlag::eDYNAMIC | PxSceneQueryFilterFlag::eSTATIC,
							PxSceneQueryFilterCallback* filterCall = NULL,
							const PxSceneQueryCache* cache = NULL,
							PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sweep returning a single result.
	
	Returns the first rigid actor that is hit along the ray. Data for a blocking hit will be returned as specified by the outputFlags field. Touching hits will be ignored.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometry		Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose			Pose of the sweep object.
	\param[in] unitDir		Normalized direction of the sweep.
	\param[in] distance		Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags	Specifies which properties should be written to the hit information.
	\param[out] hit			Sweep hit information.
	\param[in] filterData	Filtering data and simple logic.
	\param[in] filterCall	Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache		Cached hit shape (optional). Sweep is performed against cached shape first then against the scene.
							Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient	ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFlags PxSweepHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual bool sweepSingle(	const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
								PxSceneQueryFlags outputFlags,
								PxSweepHit& hit,
								const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
								PxSceneQueryFilterCallback* filterCall = NULL,
								const PxSceneQueryCache* cache = NULL,
								PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sweep of geometry compound returning a single result.
	
	The function sweeps all specified geometry objects through space and returns the first rigid actor that is hit along the ray.
	Data for a blocking hit will be returned as specified by the outputFlags field. Touching hits will be ignored.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometryList		Geometries of objects to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] poseList			The world pose for each geometry object.
	\param[in] filterDataList	Filter data for each geometry object. NULL, if no filtering should be done, all hits are assumed to be blocking in that case.
	\param[in] geometryCount	Number of geometry objects specified.
	\param[in] unitDir			Normalized direction of the sweep.
	\param[in] distance			Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags		Specifies which properties should be written to the hit information.
	\param[out] hit				Sweep hit information.
	\param[in] filterFlags		Simple filter logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be blocking.
	\param[in] cache			Cached hit shape (optional). Sweep is performed against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return True if a blocking hit was found.

	@see PxSceneQueryFlags PxSweepHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual bool sweepSingle(	const PxGeometry** geometryList, const PxTransform* poseList, const PxFilterData* filterDataList, PxU32 geometryCount, 
								const PxVec3& unitDir, const PxReal distance,
								PxSceneQueryFlags outputFlags,
								PxSweepHit& hit,
								PxSceneQueryFilterFlags filterFlags = PxSceneQueryFilterFlag::eDYNAMIC | PxSceneQueryFilterFlag::eSTATIC,
								PxSceneQueryFilterCallback* filterCall = NULL,
								const PxSceneQueryCache* cache = NULL,
								PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sweep returning multiple results.
	
	Find all rigid actors that get hit along the sweep. Each result contains data as specified by the outputFlags field.

	\note Touching hits are not ordered.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometry			Geometry of object to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the sweep object.
	\param[in] unitDir			Normalized direction of the sweep.
	\param[in] distance			Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags		Specifies which properties should be written to the hit information.
	\param[out] hitBuffer		Sweep hit information buffer. If the buffer overflows, the blocking hit is returned as the last entry together with an arbitrary subset
								of the nearer touching hits (typically the query should be restarted with a larger buffer).
	\param[in] hitBufferSize	Size of the hit buffer.
	\param[out] blockingHit		True if a blocking hit was found. If found, it is the last in the buffer, preceded by any touching hits which are closer. Otherwise the touching hits are listed.
	\param[in] filterData		Filtering data and simple logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be touching.
	\param[in] cache			Cached hit shape (optional). Sweep is performed against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return Number of hits in the buffer, or -1 if the buffer overflowed.

	@see PxSceneQueryFlags PxSweepHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual PxI32 sweepMultiple(	const PxGeometry& geometry, const PxTransform& pose, const PxVec3& unitDir, const PxReal distance,
									PxSceneQueryFlags outputFlags,
									PxSweepHit* hitBuffer,
									PxU32 hitBufferSize,
									bool& blockingHit,
									const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
									PxSceneQueryFilterCallback* filterCall = NULL,
									const PxSceneQueryCache* cache = NULL,
									PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Sweep of geometry compound returning multiple results.
	
	The function sweeps all specified geometry objects through space and finds all rigid actors that get hit along the sweep. 
	Each result contains data as specified by the outputFlags field.

	\note Touching hits are not ordered.

	\note If a shape from the scene is already overlapping with the query shape in its starting position, behavior is controlled by the PxSceneQueryFlag::eINITIAL_OVERLAP flag.

	\param[in] geometryList		Geometries of objects to sweep (supported types are: box, sphere, capsule, convex).
	\param[in] poseList			The world pose for each geometry object.
	\param[in] filterDataList	Filter data for each geometry object. NULL, if no filtering should be done, all hits are assumed to be blocking in that case.
	\param[in] geometryCount	Number of geometry objects specified.
	\param[in] unitDir			Normalized direction of the sweep.
	\param[in] distance			Sweep distance. Needs to be larger than 0. Will be clamped to PX_MAX_SWEEP_DISTANCE.
	\param[in] outputFlags		Specifies which properties should be written to the hit information.
	\param[out] hitBuffer		Sweep hit information buffer. If the buffer overflows, the blocking hit is returned as the last entry together with an arbitrary subset
								of the nearer touching hits (typically the query should be restarted with a larger buffer).
	\param[in] hitBufferSize	Size of the hit buffer.
	\param[out] blockingHit		True if a blocking hit was found. If found, it is the last in the buffer, preceded by any touching hits which are closer. Otherwise the touching hits are listed.
	\param[in] filterFlags		Simple filter logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to be touching.
	\param[in] cache			Cached hit shape (optional). Sweep is performed against cached shape first then against the scene.
								Note: Filtering is not executed for a cached shape if supplied; instead, if a hit is found, it is assumed to be a blocking hit.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return Number of hits in the buffer, or -1 if the buffer overflowed.

	@see PxSceneQueryFlags PxSweepHit PxSceneQueryFilterData PxSceneQueryFilterCallback PxSceneQueryCache
	*/
	virtual PxI32 sweepMultiple(const PxGeometry** geometryList, const PxTransform* poseList, const PxFilterData* filterDataList, PxU32 geometryCount, 
								const PxVec3& unitDir, const PxReal distance,
								PxSceneQueryFlags outputFlags,
								PxSweepHit* hitBuffer,
								PxU32 hitBufferSize,
								bool& blockingHit,
								PxSceneQueryFilterFlags filterFlags = PxSceneQueryFilterFlag::eDYNAMIC | PxSceneQueryFilterFlag::eSTATIC,
								PxSceneQueryFilterCallback* filterCall = NULL,
								const PxSceneQueryCache* cache = NULL,
								PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;


	/**
	\brief Test overlap between a geometry and objects in the scene.
	
	\note Filtering: Overlap tests do not distinguish between touching and blocking hit types (see #PxSceneQueryHitType). Both get written to the hit buffer.

	\note PxSceneQueryFilterFlag::eMESH_MULTIPLE and PxSceneQueryFilterFlag::eBACKFACE have no effect in this case

	\param[in] geometry			Geometry of object to check for overlap (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the object.
	\param[out] hitBuffer		Buffer to store the overlapping objects to. If the buffer overflows, an arbitrary subset of overlapping objects is stored (typically the query should be restarted with a larger buffer).
	\param[in] hitBufferSize	Size of the hit buffer. 
	\param[in] filterData		Filtering data and simple logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to overlap.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return Number of hits in the buffer, or -1 if the buffer overflowed.

	@see PxSceneQueryFlags PxSceneQueryFilterData PxSceneQueryFilterCallback
	*/
	virtual PxI32 overlapMultiple(	const PxGeometry& geometry,
									const PxTransform& pose,
									PxShape** hitBuffer,
									PxU32 hitBufferSize,
									const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
									PxSceneQueryFilterCallback* filterCall = NULL,
									PxClientID queryClient = PX_DEFAULT_CLIENT) const = 0;

	/**
	\brief Test returning, for a given geometry, any overlapping object in the scene.
	
	\note Filtering: Overlap tests do not distinguish between touching and blocking hit types (see #PxSceneQueryHitType). Both trigger a hit.

	\note PxSceneQueryFilterFlag::eMESH_MULTIPLE and PxSceneQueryFilterFlag::eBACKFACE have no effect in this case
	
	\param[in] geometry			Geometry of object to check for overlap (supported types are: box, sphere, capsule, convex).
	\param[in] pose				Pose of the object.
	\param[out] hit				Pointer to store the overlapping object to.
	\param[in] filterData		Filtering data and simple logic.
	\param[in] filterCall		Custom filtering logic (optional). Only used if the corresponding #PxSceneQueryFilterFlag flags are set. If NULL, all hits are assumed to overlap.
	\param[in] queryClient		ID of the client doing the query (see #createClient())
	\return True if an overlap was found.

	@see PxSceneQueryFlags PxSceneQueryFilterData PxSceneQueryFilterCallback
	*/
	PX_INLINE bool overlapAny(const PxGeometry& geometry,
							const PxTransform& pose,
							PxShape*& hit,
							const PxSceneQueryFilterData& filterData = PxSceneQueryFilterData(),
							PxSceneQueryFilterCallback* filterCall = NULL,
							PxClientID queryClient = PX_DEFAULT_CLIENT) const
	{ return overlapMultiple(geometry, pose,&hit,1,filterData,filterCall,queryClient) != 0 ? true : false; }

	


//@}

/************************************************************************************************/

/** @name Visualization and Statistics
*/
//@{
	
	/**
	\brief Function that lets you set debug visualization parameters.

	Returns false if the value passed is out of range for usage specified by the enum.

	\param[in] param	Parameter to set. See #PxVisualizationParameter
	\param[in] value	The value to set, see #PxVisualizationParameter for allowable values.
	\return False if the parameter is out of range.

	@see getVisualizationParameter PxVisualizationParameter
	*/
	virtual bool setVisualizationParameter(PxVisualizationParameter::Enum param, PxReal value) = 0;

	/**
	\brief Function that lets you query debug visualization parameters.

	\param[in] paramEnum The Parameter to retrieve.
	\return The value of the parameter.

	@see setVisualizationParameter PxVisualizationParameter
	*/
	virtual PxReal getVisualizationParameter(PxVisualizationParameter::Enum paramEnum) const = 0;


	virtual void setVisualizationCullingBox(const PxBounds3& box) = 0;
	virtual const PxBounds3& getVisualizationCullingBox() const = 0;

	/**
	\brief Retrieves the render buffer.
	
	This will contain the results of any active visualization for this scene.

	\note Do not use this method while the simulation is running. Calls to this method while result in undefined behaviour.

	\return The render buffer.

	@see PxRenderBuffer
	*/
	virtual const PxRenderBuffer&	getRenderBuffer() = 0;

	
	/**
	\brief Call this method to retrieve statistics for the current simulation step.

	\note Do not use this method while the simulation is running. Calls to this method while the simulation is running will be ignored.

	\param[out] stats Used to retrieve statistics for the current simulation step.

	@see PxSimulationStatistics
	*/
	virtual	void					getSimulationStatistics(PxSimulationStatistics& stats) const = 0;

//@}
	
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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
// Copyright (c) 2008-2014 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_NX_ACTOR
#define PX_PHYSICS_NX_ACTOR

/** \addtogroup physics
  @{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxBounds3.h"
#include "PxClient.h"
#include "common/PxBase.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxRigidActor;
class PxRigidBody;
class PxRigidStatic;
class PxRigidDynamic;
class PxParticleBase;
class PxParticleSystem;
class PxParticleFluid;
class PxArticulation;
class PxArticulationLink;


/** Group index which allows to specify 1- or 2-way interaction */
typedef PxU8 PxDominanceGroup;		// Must be < 32, PxU8.

/**
\brief Flags which control the behavior of an actor.

@see PxActorFlags PxActor PxActor.setActorFlag() PxActor.getActorFlags()
*/
struct PxActorFlag
{
	enum Enum
	{
		/**
		\brief Enable debug renderer for this actor

		@see PxScene.getRenderBuffer() PxRenderBuffer PxVisualizationParameter
		*/
		eVISUALIZATION					= (1<<0),

		/**
		\brief Disables scene gravity for this actor
		*/
		eDISABLE_GRAVITY				= (1<<1),

		/**
		\brief Enables the sending of PxSimulationEventCallback::onWake() and PxSimulationEventCallback::onSleep() notify events

		@see PxSimulationEventCallback::onWake() PxSimulationEventCallback::onSleep()
		*/
		eSEND_SLEEP_NOTIFIES			= (1<<2),

		/**
		\brief Disables simulation for the actor.
		
		\note This is only supported by PxRigidStatic and PxRigidDynamic actors and can be used to reduce the memory footprint when rigid actors are
		used for scene queries only.

		\note Setting this flag will remove all constraints attached to the actor from the scene.

		\note If this flag is set, the following calls are forbidden:
		\li PxRigidBody: setLinearVelocity(), setAngularVelocity(), addForce(), addTorque(), clearForce(), clearTorque()
		\li PxRigidDynamic: setKinematicTarget(), setWakeCounter(), wakeUp(), putToSleep()

		\par <b>Sleeping:</b>
		Raising this flag will set all velocities and the wake counter to 0, clear all forces, clear the kinematic target, put the actor
		to sleep and wake up all touching actors from the previous frame.
		*/
		eDISABLE_SIMULATION				= (1<<3)

	};
};

/**
\brief collection of set bits defined in PxActorFlag.

@see PxActorFlag
*/
typedef PxFlags<PxActorFlag::Enum,PxU16> PxActorFlags;
PX_FLAGS_OPERATORS(PxActorFlag::Enum,PxU16)

/**
\brief Identifies each type of actor.
@see PxActor 
*/
struct PxActorType
{
	enum Enum
	{
		/**
		\brief A static rigid body
		@see PxRigidStatic
		*/
		eRIGID_STATIC,

		/**
		\brief A dynamic rigid body
		@see PxRigidDynamic
		*/
		eRIGID_DYNAMIC,

#if PX_USE_PARTICLE_SYSTEM_API
		/**
		\brief A particle system
		@see PxParticleSystem
		*/
		ePARTICLE_SYSTEM,

		/**
		\brief A particle fluid
		@see PxParticleFluid
		*/
		ePARTICLE_FLUID,
#endif
		
		/**
		\brief An articulation link
		@see PxArticulationLink
		*/
		eARTICULATION_LINK,

#if PX_USE_CLOTH_API
		/**
		\brief A cloth
		@see PxCloth
		*/
		eCLOTH,
#endif

		//brief internal use only!
		eACTOR_COUNT,

		eACTOR_FORCE_DWORD = 0x7fffffff
	};
};

/**
\brief PxActor is the base class for the main simulation objects in the physics SDK.

The actor is owned by and contained in a PxScene.

*/
class PxActor : public PxBase
{
public:
	/**
	\brief Deletes the actor.
	
	Do not keep a reference to the deleted instance.

	If the actor belongs to a #PxAggregate object, it is automatically removed from the aggregate.

	@see PxBase.release(), PxAggregate
	*/
	virtual		void			release() = 0;

	/**
	\brief Retrieves the type of actor.

	\return The actor type of the actor.

	@see PxActorType
	*/
	virtual		PxActorType::Enum	getType()	const = 0;


	/**
	\deprecated
	\brief Attempts to cast to specific actor type.

	\return NULL if the actor is not of the appropriate type. Otherwise a pointer to the specific actor type.

	\note Since PxParticleFluid inherits from PxParticleSystem, calling isParticleSystem() on a PxParticleFluid instance will
	succeed and return the upcast to PxParticleSystem.

	@see PxActorType PxRigidActor PxRigidBody PxRigidStatic PxRigidDynamic PxParticleBase PxParticleSystem PxParticleFluid
	*/
	PX_DEPRECATED	PX_INLINE	PxRigidStatic*				isRigidStatic();
	PX_DEPRECATED	PX_INLINE	const PxRigidStatic*		isRigidStatic()			const;
	PX_DEPRECATED	PX_INLINE	PxRigidDynamic*				isRigidDynamic();
	PX_DEPRECATED	PX_INLINE	const PxRigidDynamic*		isRigidDynamic()		const;
	PX_DEPRECATED	PX_INLINE	PxParticleSystem*			isParticleSystem();
	PX_DEPRECATED	PX_INLINE	const PxParticleSystem*		isParticleSystem()		const;
	PX_DEPRECATED	PX_INLINE	PxParticleFluid*			isParticleFluid();
	PX_DEPRECATED	PX_INLINE	const PxParticleFluid*		isParticleFluid()		const;
	PX_DEPRECATED	PX_INLINE	PxArticulationLink*			isArticulationLink();
	PX_DEPRECATED	PX_INLINE	const PxArticulationLink*	isArticulationLink()	const;
	PX_DEPRECATED	PX_INLINE	PxCloth*					isCloth();
	PX_DEPRECATED	PX_INLINE	const PxCloth*				isCloth()				const;

	PX_DEPRECATED	PX_INLINE	PxRigidActor*				isRigidActor();
	PX_DEPRECATED	PX_INLINE	const PxRigidActor*			isRigidActor()			const;
	PX_DEPRECATED	PX_INLINE	PxRigidBody*				isRigidBody();
	PX_DEPRECATED	PX_INLINE	const PxRigidBody*			isRigidBody()			const;
	PX_DEPRECATED	PX_INLINE	PxParticleBase*				isParticleBase();
	PX_DEPRECATED	PX_INLINE	const PxParticleBase*		isParticleBase()		const;

	/**
	\brief Retrieves the scene which this actor belongs to.

	\return Owner Scene. NULL if not part of a scene.

	@see PxScene
	*/
	virtual		PxScene*		getScene()	const = 0;

	// Runtime modifications

	/**
	\brief Sets a name string for the object that can be retrieved with getName().
	
	This is for debugging and is not used by the SDK. The string is not copied by the SDK, 
	only the pointer is stored.

	\param[in] name String to set the objects name to.

	<b>Default:</b> NULL

	@see getName()
	*/
	virtual		void			setName(const char* name)		= 0;

	/**
	\brief Retrieves the name string set with setName().

	\return Name string associated with object.

	@see setName()
	*/
	virtual		const char*		getName()			const	= 0;

	/**
	\brief Retrieves the axis aligned bounding box enclosing the actor.

	\param[in] inflation  Scale factor for computed world bounds. Box extents are multiplied by this value.

	\return The actor's bounding box.

	@see PxBounds3
	*/
	virtual		PxBounds3		getWorldBounds(float inflation=1.01f) const = 0;

	/**
	\brief Raises or clears a particular actor flag.
	
	See the list of flags #PxActorFlag

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] flag  The PxActor flag to raise(set) or clear. See #PxActorFlag.
	\param[in] value The boolean value to assign to the flag.

	<b>Default:</b> PxActorFlag::eVISUALIZATION

	@see PxActorFlag getActorFlags() 
	*/
	virtual		void			setActorFlag(PxActorFlag::Enum flag, bool value) = 0;
	/**
	\brief sets the actor flags
	
	See the list of flags #PxActorFlag
	@see PxActorFlag setActorFlag() 
	*/
	virtual		void			setActorFlags( PxActorFlags inFlags ) = 0;

	/**
	\brief Reads the PxActor flags.
	
	See the list of flags #PxActorFlag

	\return The values of the PxActor flags.

	@see PxActorFlag setActorFlag() 
	*/
	virtual		PxActorFlags	getActorFlags()	const = 0;

	/**
	\brief Assigns dynamic actors a dominance group identifier.
	
	PxDominanceGroup is a 5 bit group identifier (legal range from 0 to 31).
	
	The PxScene::setDominanceGroupPair() lets you set certain behaviors for pairs of dominance groups.
	By default every dynamic actor is created in group 0.

	<b>Default:</b> 0

	<b>Sleeping:</b> Changing the dominance group does <b>NOT</b> wake the actor up automatically.

	\param[in] dominanceGroup The dominance group identifier. <b>Range:</b> [0..31]

	@see getDominanceGroup() PxDominanceGroup PxScene::setDominanceGroupPair()
	*/
	virtual		void			setDominanceGroup(PxDominanceGroup dominanceGroup)		 = 0;
	
	/**
	\brief Retrieves the value set with setDominanceGroup().

	\return The dominance group of this actor.

	@see setDominanceGroup() PxDominanceGroup PxScene::setDominanceGroupPair()
	*/
	virtual		PxDominanceGroup	getDominanceGroup() const = 0;

	
	/**
	\brief Sets the owner client of an actor.

	This cannot be done once the actor has been placed into a scene.

	<b>Default:</b> PX_DEFAULT_CLIENT

	@see PxClientID PxScene::createClient() 
	*/
	virtual		void			setOwnerClient( PxClientID inClient ) = 0;

	/**
	\brief Returns the owner client that was specified with at creation time.

	This value cannot be changed once the object is placed into the scene.

	@see PxClientID PxScene::createClient()
	*/
	virtual		PxClientID		getOwnerClient() const = 0;

	/**
	\brief Sets the behavior bits of the actor.

	The behavior bits determine which types of events the actor will broadcast to foreign clients.
	The actor will always send notice for all possible events to its own owner client.  By default
	it will not send any events to any other clients.  If the user however raises a bit flag for
	any event type using this function, that event will then be sent also to any other clients which
	are programmed to listed to foreign actor events of that type.  

	<b>Default:</b> 0

	@see PxClientID PxActorClientBehaviorFlag PxScene::setClientBehaviorFlags()
	*/
	virtual		void			setClientBehaviorFlags(PxActorClientBehaviorFlags) = 0;

	/**
	\brief Retrieves the behavior bits of the actor.

	The behavior bits determine which types of events the actor will broadcast to foreign clients.

	@see PxActorClientBehaviorFlag setClientBehaviorFlags()
	*/
	virtual		PxActorClientBehaviorFlags	getClientBehaviorFlags()	const = 0;

	/**
	\brief Retrieves the aggregate the actor might be a part of.

	\return The aggregate the actor is a part of, or NULL if the actor does not belong to an aggregate.

	@see PxAggregate
	*/
	virtual		PxAggregate*	getAggregate()	const = 0;

	//public variables:
				void*			userData;	//!< user can assign this to whatever, usually to create a 1:1 relationship with a user object.


protected:
	PX_INLINE					PxActor(PxType concreteType, PxBaseFlags baseFlags) : PxBase(concreteType, baseFlags), userData(NULL) {}
	PX_INLINE					PxActor(PxBaseFlags baseFlags) : PxBase(baseFlags) {}
	virtual						~PxActor()	{}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxActor", name) || PxBase::isKindOf(name); }


};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

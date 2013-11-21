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


#ifndef PX_PHYSICS_NX_RIGIDDYNAMIC
#define PX_PHYSICS_NX_RIGIDDYNAMIC
/** \addtogroup physics
@{
*/

#include "PxRigidBody.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Collection of flags describing the behavior of a dynamic rigid body.

@see PxRigidDynamic.setRigidDynamicFlag(), PxRigidDynamic.getRigidDynamicFlags()
*/
struct PxRigidDynamicFlag
{
	enum Enum
	{

		/**
		\brief Enables kinematic mode for the actor.

		Kinematic actors are special dynamic actors that are not 
		influenced by forces (such as gravity), and have no momentum. They are considered to have infinite
		mass and can be moved around the world using the setKinematicTarget() method. They will push 
		regular dynamic actors out of the way. Kinematics will not collide with static or other kinematic objects.

		Kinematic actors are great for moving platforms or characters, where direct motion control is desired.

		You can not connect Reduced joints to kinematic actors. Lagrange joints work ok if the platform
		is moving with a relatively low, uniform velocity.

		@see PxRigidDynamic.setKinematicTarget()
		*/
		eKINEMATIC				= (1<<0),		//!< Enable kinematic mode for the body.

	};
};

/**
\brief collection of set bits defined in PxRigidDynamicFlag.

@see PxRigidDynamicFlag
*/
typedef PxFlags<PxRigidDynamicFlag::Enum,PxU16> PxRigidDynamicFlags;
PX_FLAGS_OPERATORS(PxRigidDynamicFlag::Enum,PxU16);

/**
\brief PxRigidDynamic represents a dynamic rigid simulation object in the physics SDK.

<h3>Creation</h3>
Instances of this class are created by calling #PxPhysics::createRigidDynamic() and deleted with #release().


<h3>Visualizations</h3>
\li #PxVisualizationParameter::eACTOR_AXES
\li #PxVisualizationParameter::eBODY_AXES
\li #PxVisualizationParameter::eBODY_MASS_AXES
\li #PxVisualizationParameter::eBODY_LIN_VELOCITY
\li #PxVisualizationParameter::eBODY_ANG_VELOCITY
\li #PxVisualizationParameter::eBODY_JOINT_GROUPS

@see PxRigidBody  PxPhysics.createRigidDynamic()  release()
*/

class PxRigidDynamic : public PxRigidBody
{
public:
	// Runtime modifications


/************************************************************************************************/
/** @name Kinematic Actors
*/

	/**
	\brief Moves kinematically controlled dynamic actors through the game world.

	You set a dynamic actor to be kinematic using the PxRigidDynamicFlag::eKINEMATIC flag,
	used either in the PxRigidDynamicDesc or with setRigidDynamicFlag().
	
	The move command will result in a velocity that will move the body into 
	the desired pose. After the move is carried out during a single time step, 
	the velocity is returned to zero. Thus, you must continuously call 
	this in every time step for kinematic actors so that they move along a path.
	
	This function simply stores the move destination until the next simulation
	step is processed, so consecutive calls will simply overwrite the stored target variable.

	The motion is always fully carried out.	

	<b>Sleeping:</b> This call wakes the actor if it is sleeping.

	\param[in] destination The desired pose for the kinematic actor, in the global frame. <b>Range:</b> rigid body transform.

	@see PxRigidDynamicFlag setRigidDynamicFlag()
	*/
	virtual		void				setKinematicTarget(const PxTransform& destination) = 0;

/************************************************************************************************/
/** @name Damping
*/

	/**
	\brief Sets the linear damping coefficient.
	
	Zero represents no damping. The damping coefficient must be nonnegative.

	<b>Default:</b> 0.0
	
	\param[in] linDamp Linear damping coefficient. <b>Range:</b> [0,inf)

	@see getLinearDamping() setAngularDamping()
	*/
	virtual		void				setLinearDamping(PxReal linDamp) = 0;

	/**
	\brief Retrieves the linear damping coefficient.

	\return The linear damping coefficient associated with this actor.

	@see setLinearDamping() getAngularDamping()
	*/
	virtual		PxReal				getLinearDamping() const = 0;

	/**
	\brief Sets the angular damping coefficient.
	
	Zero represents no damping.
	
	The angular damping coefficient must be nonnegative.

	<b>Default:</b> 0.05

	\param[in] angDamp Angular damping coefficient. <b>Range:</b> [0,inf)

	@see getAngularDamping() setLinearDamping()
	*/
	virtual		void				setAngularDamping(PxReal angDamp) = 0;

	/**
	\brief Retrieves the angular damping coefficient.

	\return The angular damping coefficient associated with this actor.

	@see setAngularDamping() getLinearDamping()
	*/
	virtual		PxReal				getAngularDamping() const = 0;

/************************************************************************************************/
/** @name Velocity
*/

	/**
	\brief Lets you set the maximum angular velocity permitted for this actor.
	
	For various internal computations, very quickly rotating actors introduce error 
	into the simulation, which leads to undesired results.

	With this function, you can set the  maximum angular velocity permitted for this rigid body. 
	Higher angular velocities are clamped to this value. 

	Note: The angular velocity is clamped to the set value <i>before</i> the solver, which means that
	the limit may still be momentarily exceeded.

	<b>Default:</b> 7.0

	\param[in] maxAngVel Max allowable angular velocity for actor. <b>Range:</b> (0,inf)

	@see getMaxAngularVelocity()
	*/
	virtual		void				setMaxAngularVelocity(PxReal maxAngVel) = 0;

	/**
	\brief Retrieves the maximum angular velocity permitted for this actor.

	\return The maximum allowed angular velocity for this actor.

	@see setMaxAngularVelocity
	*/
	virtual		PxReal				getMaxAngularVelocity()	const = 0; 

/************************************************************************************************/
/** @name Sleeping
*/

	/**
	\brief Returns true if this body is sleeping.

	When an actor does not move for a period of time, it is no longer simulated in order to save time. This state
	is called sleeping. However, because the object automatically wakes up when it is either touched by an awake object,
	or one of its properties is changed by the user, the entire sleep mechanism should be transparent to the user.
	
	If an actor is asleep after the call to PxScene::fetchResults() returns, it is guaranteed that the pose of the actor 
	was not changed. You can use this information to avoid updating the transforms of associated of dependent objects.

	\return True if the actor is sleeping.

	@see isSleeping() wakeUp() putToSleep()  getSleepThreshold()
	*/
	virtual		bool				isSleeping() const = 0;


    /**
	\brief Sets the mass-normalized kinetic energy threshold below which an actor may go to sleep.

	Actors whose kinetic energy divided by their mass is above this threshold will not be put to sleep.

	<b>Default:</b> 0.05 * PxTolerancesScale::speed * PxTolerancesScale::speed

	\param[in] threshold Energy below which an actor may go to sleep. <b>Range:</b> (0,inf]

	@see isSleeping() getSleepThreshold() wakeUp() putToSleep() PxTolerancesScale
	*/
	virtual		void				setSleepThreshold(PxReal threshold) = 0;

	/**
	\brief Returns the mass-normalized kinetic energy below which an actor may go to sleep.

	Actors whose kinetic energy divided by their mass is above this threshold will not be put to sleep. 

	\return The energy threshold for sleeping.

	@see isSleeping() wakeUp() putToSleep() setSleepThreshold()
	*/
	virtual		PxReal				getSleepThreshold() const = 0;

	/**
	\brief Wakes up the actor if it is sleeping.  

	The wakeCounterValue determines how long until the body is put to sleep, a value of zero means 
	that the body is sleeping. wakeUp(0) is equivalent to PxRigidDynamic::putToSleep().

	\param[in] wakeCounterValue New sleep counter value. <b>Range:</b> [0,inf]

	@see isSleeping() putToSleep()
	*/
	virtual		void				wakeUp(PxReal wakeCounterValue=PX_SLEEP_INTERVAL)	= 0;

	/**
	\brief Forces the actor to sleep. 
	
	The actor will stay asleep during the next simulation step if not touched by another non-sleeping actor.
	
	\note This will set the velocity of the actor to 0.

	@see isSleeping() wakeUp()
	*/
	virtual		void				putToSleep()	= 0;

/************************************************************************************************/

    /**
	\brief Sets the solver iteration counts for the body. 
	
	The solver iteration count determines how accurately joints and contacts are resolved. 
	If you are having trouble with jointed bodies oscillating and behaving erratically, then
	setting a higher position iteration count may improve their stability.

	If intersecting bodies are being depenetrated too violently, increase the number of velocity iterations.

	<b>Default:</b> 4 position iterations, 1 velocity iteration

	\param[in] minPositionIters Number of position iterations the solver should perform for this body. <b>Range:</b> [1,255]
	\param[in] minVelocityIters Number of velocity iterations the solver should perform for this body. <b>Range:</b> [1,255]

	@see getSolverIterationCounts()
	*/
	virtual		void				setSolverIterationCounts(PxU32 minPositionIters, PxU32 minVelocityIters = 1) = 0;

	/**
	\brief Retrieves the solver iteration counts.

	@see setSolverIterationCounts()
	*/
	virtual		void				getSolverIterationCounts(PxU32& minPositionIters, PxU32& minVelocityIters) const = 0;

	/**
	\brief Retrieves the force threshold for contact reports.

	The contact report threshold is a force threshold. If the force between 
	two actors exceeds this threshold for either of the two actors, a contact report 
	will be generated according to the contact report threshold flags provided by
	the filter shader/callback.
	See #PxPairFlag.

	The threshold used for a collision between a dynamic actor and the static environment is 
    the threshold of the dynamic actor, and all contacts with static actors are summed to find 
    the total normal force.

	<b>Default:</b> PX_MAX_F32

	\return Force threshold for contact reports.

	@see setContactReportThreshold PxPairFlag PxSimulationFilterShader PxSimulationFilterCallback
	*/
	virtual     PxReal				getContactReportThreshold() const = 0;

	/**
	\brief Sets the force threshold for contact reports.

	See #getContactReportThreshold().

	\param[in] threshold Force threshold for contact reports. <b>Range:</b> (0,inf)

	@see getContactReportThreshold PxPairFlag
	*/
	virtual     void				setContactReportThreshold(PxReal threshold) = 0;

    /**
	\brief Raises or clears a particular dynamic rigid body flag.
	
	See the list of flags #PxRigidDynamicFlag

	<b>Default:</b> no flags are set

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] flag		The PxRigidDynamic flag to raise(set) or clear. See #PxRigidDynamicFlag.
	\param[in] value	The new boolean value for the flag.

	@see PxRigidDynamicFlag getRigidDynamicFlags() 
	*/
	virtual		void				setRigidDynamicFlag(PxRigidDynamicFlag::Enum flag, bool value) = 0;
	virtual		void				setRigidDynamicFlags(PxRigidDynamicFlags inFlags) = 0;

	/**
	\brief Reads the PxRigidDynamic flags.
	
	See the list of flags #PxRigidDynamicFlag

	\return The values of the PxRigidDynamic flags.

	@see PxRigidDynamicFlag setRigidDynamicFlag()
	*/
	virtual		PxRigidDynamicFlags	getRigidDynamicFlags()	const = 0;

	virtual		const char*		getConcreteTypeName() const					{	return "PxRigidDynamic"; }

protected:
								PxRigidDynamic(PxRefResolver& v) : PxRigidBody(v)		{}
	PX_INLINE					PxRigidDynamic() : PxRigidBody() {}
	virtual						~PxRigidDynamic()	{}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxRigidDynamic", name) || PxRigidBody::isKindOf(name); }

};

PX_INLINE	PxRigidDynamic*			PxActor::isRigidDynamic()				{ return is<PxRigidDynamic>();		}
PX_INLINE	const PxRigidDynamic*	PxActor::isRigidDynamic()		const	{ return is<PxRigidDynamic>();		}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

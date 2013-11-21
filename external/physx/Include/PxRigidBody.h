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


#ifndef PX_PHYSICS_NX_RIGIDBODY
#define PX_PHYSICS_NX_RIGIDBODY
/** \addtogroup physics
@{
*/

#include "PxRigidActor.h"
#include "PxForceMode.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief PxRigidBody is a base class shared between dynamic rigid body objects.

@see PxRigidActor
*/

class PxRigidBody : public PxRigidActor
{
public:
	// Runtime modifications

/************************************************************************************************/
/** @name Mass Manipulation
*/

	/**
	\brief Sets the pose of the center of mass relative to the actor.	
	
	\note Changing this transform will not move the actor in the world!

	\note Setting an unrealistic center of mass which is a long way from the body can make it difficult for
	the SDK to solve constraints. Perhaps leading to instability and jittering bodies.

	<b>Default:</b> the identity transform

	<b>Sleeping:</b> This call wakes the actor if it is sleeping.

	\param[in] pose Mass frame offset transform relative to the actor frame. <b>Range:</b> rigid body transform.

	@see getCMassLocalPose() PxRigidBodyDesc.massLocalPose
	*/
	virtual		void			setCMassLocalPose(const PxTransform& pose) = 0;


	/**
	\brief Retrieves the center of mass pose relative to the actor frame.

	\return The center of mass pose relative to the actor frame.

	@see setCMassLocalPose() PxRigidBodyDesc.massLocalPose
	*/
	virtual		PxTransform 	getCMassLocalPose() const = 0;


	/**
	\brief Sets the mass of a dynamic actor.
	
	The mass must be positive.
	
	setMass() does not update the inertial properties of the body, to change the inertia tensor
	use setMassSpaceInertiaTensor() or the PhysX extensions method #PxRigidBodyExt::updateMassAndInertia().

	<b>Default:</b> 1.0

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] mass New mass value for the actor. <b>Range:</b> (0,inf)

	@see getMass() PxRigidBodyDesc.mass setMassSpaceInertiaTensor()
	*/
	virtual		void			setMass(PxReal mass) = 0;

	/**
	\brief Retrieves the mass of the actor.

	\return The mass of this actor.

	@see setMass() PxRigidBodyDesc.mass setMassSpaceInertiaTensor()
	*/
	virtual		PxReal			getMass() const = 0;

	/**
	\brief Sets the inertia tensor, using a parameter specified in mass space coordinates.
	
	Note that such matrices are diagonal -- the passed vector is the diagonal.

	If you have a non diagonal world/actor space inertia tensor(3x3 matrix). Then you need to
	diagonalize it and set an appropriate mass space transform. See #setCMassLocalPose().

	<b>Default:</b> (1.0, 1.0, 1.0)

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] m New mass space inertia tensor for the actor. <b>Range:</b> inertia vector

	@see PxRigidBodyDesc.massSpaceInertia getMassSpaceInertia() setMass() setCMassLocalPose()
	*/
	virtual		void			setMassSpaceInertiaTensor(const PxVec3& m) = 0;

	/**
	\brief  Retrieves the diagonal inertia tensor of the actor relative to the mass coordinate frame.

	This method retrieves a mass frame inertia vector.

	\return The mass space inertia tensor of this actor.

	@see PxRigidBodyDesc.massSpaceInertia setMassSpaceInertiaTensor() setMass() setCMassLocalPose()
	*/
	virtual		PxVec3			getMassSpaceInertiaTensor()			const = 0;


/************************************************************************************************/
/** @name Velocity
*/


	/**
	\brief Retrieves the linear velocity of an actor.

	\return The linear velocity of the actor.

	@see PxRigidDynamic.setLinearVelocity() getAngularVelocity()
	*/
	virtual		PxVec3			getLinearVelocity()		const = 0;

	/**
	\brief Sets the linear velocity of the actor.
	
	Note that if you continuously set the velocity of an actor yourself, 
	forces such as gravity or friction will not be able to manifest themselves, because forces directly
	influence only the velocity/momentum of an actor.

	<b>Default:</b> (0.0, 0.0, 0.0)

	<b>Sleeping:</b> This call wakes the actor if it is sleeping, the autowake parameter is true (default), and the 
	new velocity is non-zero

	\param[in] linVel New linear velocity of actor. <b>Range:</b> velocity vector
	\param[in] autowake Whether to wake the object up if it is asleep and the velocity is non-zero

	@see getLinearVelocity() setAngularVelocity()
	*/
	virtual		void			setLinearVelocity(const PxVec3& linVel, bool autowake = true ) = 0;



	/**
	\brief Retrieves the angular velocity of the actor.

	\return The angular velocity of the actor.

	@see PxRigidDynamic.setAngularVelocity() getLinearVelocity() 
	*/
	virtual		PxVec3			getAngularVelocity()	const = 0;


	/**
	\brief Sets the angular velocity of the actor.
	
	Note that if you continuously set the angular velocity of an actor yourself, 
	forces such as friction will not be able to rotate the actor, because forces directly influence only the velocity/momentum.

	<b>Default:</b> (0.0, 0.0, 0.0)

	<b>Sleeping:</b> This call wakes the actor if it is sleeping, the autowake parameter is true (default), and the 
	new velocity is non-zero

	\param[in] angVel New angular velocity of actor. <b>Range:</b> angular velocity vector
	\param[in] autowake Whether to wake the object up if it is asleep and the velocity is non-zero

	@see getAngularVelocity() setLinearVelocity() 
	*/
	virtual		void			setAngularVelocity(const PxVec3& angVel, bool autowake = true ) = 0;

	
/************************************************************************************************/
/** @name Forces
*/

	/**
	\brief Applies a force (or impulse) defined in the global coordinate frame to the actor.

	<b>This will not induce a torque</b>.

	::PxForceMode determines if the force is to be conventional or impulsive.

	\note The force modes PxForceMode::eIMPULSE and PxForceMode::eVELOCITY_CHANGE can not be applied to articulation links

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the autowake parameter is true (default).

	\param[in] force Force/Impulse to apply defined in the global frame. <b>Range:</b> force vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode)
	\param[in] autowake Specify if the call should wake up the actor if it is currently asleep.

	@see PxForceMode addTorque
	*/
	virtual		void			addForce(const PxVec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE, bool autowake = true) = 0;

	/**
	\brief Applies an impulsive torque defined in the global coordinate frame to the actor.

	::PxForceMode determines if the torque is to be conventional or impulsive.

	\note The force modes PxForceMode::eIMPULSE and PxForceMode::eVELOCITY_CHANGE can not be applied to articulation links

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the autowake parameter is true (default).

	\param[in] torque Torque to apply defined in the global frame. <b>Range:</b> torque vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode).
	\param[in] autowake whether to wake up the object if it is asleep

	@see PxForceMode addForce()
	*/
	virtual		void			addTorque(const PxVec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE, bool autowake = true) = 0;

	/**
	\brief Clears the accumulated forces (sets the accumulated force back to zero).

	::PxForceMode determines if the cleared force is to be conventional or impulsive.

	\note The force modes PxForceMode::eIMPULSE and PxForceMode::eVELOCITY_CHANGE can not be applied to articulation links

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the autowake parameter is true (default).

	\param[in] mode The mode to use when clearing the force/impulse(see #PxForceMode)
	\param[in] autowake Specify if the call should wake up the actor if it is currently asleep.

	@see PxForceMode addForce
	*/
	virtual		void			clearForce(PxForceMode::Enum mode = PxForceMode::eFORCE, bool autowake = true) = 0;

	/**
	\brief Clears the impulsive torque defined in the global coordinate frame to the actor.

	::PxForceMode determines if the cleared torque is to be conventional or impulsive.

	\note The force modes PxForceMode::eIMPULSE and PxForceMode::eVELOCITY_CHANGE can not be applied to articulation links

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the autowake parameter is true (default).

	\param[in] mode The mode to use when clearing the force/impulse(see #PxForceMode).
	\param[in] autowake whether to wake up the object if it is asleep

	@see PxForceMode addTorque
	*/
	virtual		void			clearTorque(PxForceMode::Enum mode = PxForceMode::eFORCE, bool autowake = true) = 0;

protected:
								PxRigidBody(PxRefResolver& v) : PxRigidActor(v)		{}
	PX_INLINE					PxRigidBody() : PxRigidActor() {}
	virtual						~PxRigidBody()	{}
	virtual		bool			isKindOf(const char* name)const	{	return !strcmp("PxRigidBody", name) || PxRigidActor::isKindOf(name); }
};

PX_INLINE PxRigidBody*			PxActor::isRigidBody()				{ return is<PxRigidBody>();			}
PX_INLINE const PxRigidBody*	PxActor::isRigidBody()		const	{ return is<PxRigidBody>();			}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_JOINTCONSTRAINT_H
#define PX_JOINTCONSTRAINT_H
/** \addtogroup extensions
  @{
*/

#include "foundation/PxTransform.h"
#include "PxRigidActor.h"
#include "PxConstraintDesc.h"
#include "common/PxSerialFramework.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxRigidActor;
class PxScene;
class PxPhysics;
class PxConstraint;

/**
\brief an enumeration of PhysX' built-in joint types

@see PxJoint
*/

struct PxJointType
{
	enum Enum
	{
		eD6,
		eDISTANCE,
		eFIXED,
		ePRISMATIC,
		eREVOLUTE,
		eSPHERICAL
	};
};


/**
\brief an enumeration for specifying one or other of the actors referenced by a joint

@see PxJoint
*/

struct PxJointActorIndex
{
	enum Enum
	{
		eACTOR0,
		eACTOR1,
		COUNT,
	};
};

/** 
\brief a base interface providing common functionality for PhysX joints
*/

class PxJoint
	: public PxSerializable
{
public:

	/**
	\brief Set the actors for this joint. 
	
	An actor may be NULL to indicate the world frame. At most one of the actors may be NULL.

	\param[in] actor0 the first actor.
	\param[in] actor1 the second actor

	@see getActors()
	*/

	virtual void				setActors(PxRigidActor* actor0, PxRigidActor* actor1)			= 0;

	/**
	\brief Get the actors for this joint. 
	
	\param[out] actor0 the first actor.
	\param[out] actor1 the second actor

	@see setActors()
	*/

	virtual void				getActors(PxRigidActor*& actor0, PxRigidActor*& actor1)	const	= 0;

	/**
	\brief Set the joint local pose for an actor. 
	
	This is the relative pose which locates the joint frame relative to the actor.

	\param[in] actor 0 for the first actor, 1 for the second actor.
	\param[in] localPose the local pose for the actor this joint

	@see getLocalPose()
	*/

	virtual void				setLocalPose(PxJointActorIndex::Enum actor, const PxTransform& localPose) = 0;

	/**
	\brief get the joint local pose for an actor. 
	
	\param[in] actor 0 for the first actor, 1 for the second actor.

	return the local pose for this joint

	@see setLocalPose()
	*/

	virtual PxTransform			getLocalPose(PxJointActorIndex::Enum actor) const = 0;

	/**
	\brief set the break force for this joint. 
	
	if the constraint force or torque on the joint exceeds the specified values, the joint will break, 
	at which point it will not constrain the two actors and the flag PxConstraintFlag::eBROKEN will be set. The
	force and torque are measured in the joint frame of the first actor

	\param[in] force the maximum force the joint can apply before breaking
	\param[in] torque the maximum torque the joint can apply before breaking

	@see getBreakForce() PxConstraintFlag::eBREAKABLE
	*/

	virtual void				setBreakForce(PxReal force, PxReal torque)						= 0;

	/**
	\brief get the break force for this joint. 
	
	\param[out] force the maximum force the joint can apply before breaking
	\param[out] torque the maximum torque the joint can apply before breaking

	@see setBreakForce() 
	*/
	virtual void				getBreakForce(PxReal& force, PxReal& torque)			const	= 0;

	/**
	\brief set the constraint flags for this joint. 
	
	\param[in] flags the constraint flags

	@see PxConstraintFlag
	*/
	virtual void				setConstraintFlags(PxConstraintFlags flags)						= 0;

	/**
	\brief set a constraint flags for this joint to a specified value. 
	
	\param[in] flag the constraint flag
	\param[in] value the value to which to set the flag

	@see PxConstraintFlag
	*/
	virtual void				setConstraintFlag(PxConstraintFlag::Type flag, bool value)		= 0;


	/**
	\brief get the constraint flags for this joint. 
	
	\return the constraint flags

	@see PxConstraintFlag
	*/
	virtual PxConstraintFlags	getConstraintFlags()									const	= 0;


	/**
	\brief Retrieves the PxConstraint corresponding to this joint.
	
	This can be used to determine, among other things, the force applied at the joint.

	\return the constraint
	*/

	virtual PxConstraint*		getConstraint()											const	= 0;

	/**
	\brief Sets a name string for the object that can be retrieved with getName().
	
	This is for debugging and is not used by the SDK. The string is not copied by the SDK, 
	only the pointer is stored.

	\param[in] name String to set the objects name to.

	@see getName()
	*/

	virtual void				setName(const char* name)										= 0;

	/**
	\brief Retrieves the name string set with setName().

	\return Name string associated with object.

	@see setName()
	*/

	virtual const char*			getName()												const	= 0;

	/**
	\brief Deletes the joint.
	*/

	virtual void				release()														= 0;

	/**
	\brief Retrieves the scene which this joint belongs to.

	\return Owner Scene. NULL if not part of a scene.

	@see PxScene
	*/
	virtual PxScene*			getScene()												const	= 0;

	/**
	\brief Retrieves the type of this joint.

	\return the joint type

	@see PxJointType
	*/

	virtual PxJointType::Enum	getType()												const	= 0;

	void*						userData;	//!< user can assign this to whatever, usually to create a 1:1 relationship with a user object.

								PxJoint(PxRefResolver& v)	: PxSerializable(v)	{}
	static	void				getMetaData(PxSerialStream& stream);

protected:
	virtual	~PxJoint() {}
	PxJoint() : userData(NULL)						{}
	virtual	bool				isKindOf(const char* name)	const		{	return !strcmp("PxJoint", name) || PxSerializable::isKindOf(name);			}
};

PX_DEFINE_TYPEINFO(PxFixedJoint,		PxConcreteType::eUSER_FIXED_JOINT);
PX_DEFINE_TYPEINFO(PxRevoluteJoint,		PxConcreteType::eUSER_REVOLUTE_JOINT);
PX_DEFINE_TYPEINFO(PxPrismaticJoint,	PxConcreteType::eUSER_PRISMATIC_JOINT);
PX_DEFINE_TYPEINFO(PxSphericalJoint,	PxConcreteType::eUSER_SPHERICAL_JOINT);
PX_DEFINE_TYPEINFO(PxDistanceJoint,		PxConcreteType::eUSER_DISTANCE_JOINT);
PX_DEFINE_TYPEINFO(PxD6Joint,			PxConcreteType::eUSER_D6_JOINT);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** \brief Helper function to setup a joint's global frame

	This replaces the following functions from previous SDK versions:

	void NxJointDesc::setGlobalAnchor(const NxVec3& wsAnchor);
	void NxJointDesc::setGlobalAxis(const NxVec3& wsAxis);

	The function sets the joint's localPose using world-space input parameters.

	\param[in] wsAnchor Global frame anchor point. <b>Range:</b> position vector
	\param[in] wsAxis Global frame axis. <b>Range:</b> direction vector
	\param[in,out] joint Joint having its global frame set.
*/

PX_C_EXPORT void PX_CALL_CONV PxSetJointGlobalFrame(physx::PxJoint& joint, const physx::PxVec3* wsAnchor, const physx::PxVec3* wsAxis);

/** @} */
#endif

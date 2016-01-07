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


#ifndef PX_REVOLUTEJOINT_H
#define PX_REVOLUTEJOINT_H
/** \addtogroup extensions
  @{
*/

#include "extensions/PxJoint.h"
#include "extensions/PxJointLimit.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxRevoluteJoint;

/**
\brief Create a revolute joint.

 \param[in] physics the physics SDK
 \param[in] actor0 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame0 the position and orientation of the joint relative to actor0
 \param[in] actor1 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame1 the position and orientation of the joint relative to actor1 

@see PxRevoluteJoint
*/

PxRevoluteJoint*	PxRevoluteJointCreate(PxPhysics& physics, 
										  PxRigidActor* actor0, const PxTransform& localFrame0, 
										  PxRigidActor* actor1, const PxTransform& localFrame1);


/**
\brief Flags specific to the Revolute Joint.

@see PxRevoluteJoint
*/

struct PxRevoluteJointFlag
{
	enum Enum
	{
		eLIMIT_ENABLED			= 1<<0,		//< enable the limit
		eDRIVE_ENABLED			= 1<<1,		//< enable the drive
		eDRIVE_FREESPIN			= 1<<2		//< if the existing velocity is beyond the drive velocity, do not add force
	};
};

typedef PxFlags<PxRevoluteJointFlag::Enum, PxU16> PxRevoluteJointFlags;
PX_FLAGS_OPERATORS(PxRevoluteJointFlag::Enum, PxU16)


/**

\brief A joint which behaves in a similar way to a hinge or axle.

 A hinge joint removes all but a single rotational degree of freedom from two objects.
 The axis along which the two bodies may rotate is specified with a point and a direction
 vector.

 The position of the hinge on each body is specified by the origin of the body's joint frame.
 The axis of the hinge is specified as the direction of the x-axis in the body's joint frame.
 
 \image html revoluteJoint.png

 A revolute joint can be given a motor, so that it can apply a force to rotate the attached actors.
 It may also be given a limit, to restrict the revolute motion to within a certain range. In
 addition, the bodies may be projected together if the distance or angle between them exceeds
 a given threshold.
 
 Projection, drive and limits are activated by setting the appropriate flags on the joint.

 @see PxRevoluteJointCreate() PxJoint
*/

class PxRevoluteJoint : public PxJoint
{
public:

	/**
	\brief return the angle of the joint, in the range (-Pi, Pi]
	*/
	virtual PxReal getAngle()							const			= 0;


	/**
	\brief return the velocity of the joint
	*/
	virtual PxReal getVelocity()						const			= 0;

	/**
	\brief set the joint limit parameters. 

	The limit is activated using the flag PxRevoluteJointFlag::eLIMIT_ENABLED

	The limit angle range is (-2*PI, 2*PI) and the extent of the limit must be strictly less than 2*PI

	\param[in] limits The joint limit parameters. 


	@see PxJointAngularLimitPair getLimit()
	*/

	virtual void			setLimit(const PxJointAngularLimitPair& limits)			= 0;

	/**
	\brief get the joint limit parameters.

	\return the joint limit parameters

	@see PxJointAngularLimitPair setLimit()
	*/
	virtual PxJointAngularLimitPair getLimit()			const			= 0;



	/**
	\brief set the target velocity for the drive model.

	The motor will only be able to reach this velocity if the maxForce is sufficiently large.
	If the joint is spinning faster than this velocity, the motor will actually try to brake
	(see PxRevoluteJointFlag::eDRIVE_FREESPIN.)

	If you set this to infinity then the motor will keep speeding up, unless there is some sort 
	of resistance on the attached bodies. The sign of this variable determines the rotation direction,
	with positive values going the same way as positive joint angles.

	\param[in] velocity the drive target velocity

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 0.0

	@see PxRevoluteFlags::eDRIVE_FREESPIN
	*/

	virtual void			setDriveVelocity(PxReal velocity)			= 0;

	/**
	\brief gets the target velocity for the drive model.

	\return the drive target velocity

	@see setDriveVelocity()
	*/
	virtual PxReal			getDriveVelocity()			const			= 0;


	/**
	\brief sets the maximum torque the drive can exert.
	
	Setting this to a very large value if velTarget is also very large may cause unexpected results.

	The value set here may be used either as an impulse limit or a force limit, depending on the flag PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> PX_MAX_F32

	@see setDriveVelocity()
	*/
	virtual void			setDriveForceLimit(PxReal limit)			= 0;

	/**
	\brief gets the maximum torque the drive can exert.
	
	\return the torque limit

	@see setDriveVelocity()
	*/
	virtual PxReal			getDriveForceLimit()		const			= 0;


	/**
	\brief sets the gear ratio for the drive.
	
	When setting up the drive constraint, the velocity of the first actor is scaled by this value, and its response to drive torque is scaled down.
	So if the drive target velocity is zero, the second actor will be driven to the velocity of the first scaled by the gear ratio

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 1.0

	\param[in] ratio the drive gear ratio

	@see getDriveGearRatio()
	*/
	virtual void			setDriveGearRatio(PxReal ratio)			= 0;

	/**
	\brief gets the gear ratio.
	
	\return the drive gear ratio

	@see setDriveGearRatio()
	*/
	virtual PxReal			getDriveGearRatio()		const			= 0;


	/**
	\brief sets the flags specific to the Revolute Joint.

	<b>Default</b> PxRevoluteJointFlags(0)

	\param[in] flags The joint flags.

	@see PxRevoluteJointFlag setFlag() getFlags()
	*/

	virtual void					setRevoluteJointFlags(PxRevoluteJointFlags flags) = 0;

	/**
	\brief sets a single flag specific to a Revolute Joint.

	\param[in] flag The flag to set or clear.
	\param[in] value the value to which to set the flag

	@see PxRevoluteJointFlag, getFlags() setFlags()
	*/

	virtual void					setRevoluteJointFlag(PxRevoluteJointFlag::Enum flag, bool value) = 0;

	/**
	\brief gets the flags specific to the Revolute Joint.

	\return the joint flags

	@see PxRevoluteJoint::flags, PxRevoluteJointFlag setFlag() setFlags()
	*/

	virtual PxRevoluteJointFlags	getRevoluteJointFlags(void)					const	= 0;

	/**
	\brief Set the linear tolerance threshold for projection. Projection is enabled if PxConstraintFlag::ePROJECTION
	is set for the joint.

	If the joint separates by more than this distance along its locked degrees of freedom, the solver 
	will move the bodies to close the distance.

	Setting a very small tolerance may result in simulation jitter or other artifacts.

	Sometimes it is not possible to project (for example when the joints form a cycle).

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 1e10f

	\param[in] tolerance the linear tolerance threshold

	@see getProjectionLinearTolerance() PxJoint::setConstraintFlags() PxConstraintFlag::ePROJECTION
	*/

	virtual void				setProjectionLinearTolerance(PxReal tolerance)					= 0;


	/**
	\brief Get the linear tolerance threshold for projection.

	\return the linear tolerance threshold

	@see setProjectionLinearTolerance()
	*/

	virtual PxReal				getProjectionLinearTolerance()			const					= 0;

	/**
	\brief Set the angular tolerance threshold for projection. Projection is enabled if 
	PxConstraintFlag::ePROJECTION is set for the joint.

	If the joint deviates by more than this angle around its locked angular degrees of freedom, 
	the solver will move the bodies to close the angle.
	
	Setting a very small tolerance may result in simulation jitter or other artifacts.

	Sometimes it is not possible to project (for example when the joints form a cycle).

	<b>Range:</b> [0,Pi] <br>
	<b>Default:</b> Pi

	\param[in] tolerance the angular tolerance threshold in radians

	@see getProjectionAngularTolerance() PxJoint::setConstraintFlag() PxConstraintFlag::ePROJECTION
	*/

	virtual void				setProjectionAngularTolerance(PxReal tolerance)					= 0;

	/**
	\brief gets the angular tolerance threshold for projection.

	\return the angular tolerance threshold in radians

	@see setProjectionAngularTolerance()
	*/

	virtual PxReal				getProjectionAngularTolerance()			const					= 0;

	/**
	\brief Returns string name of PxRevoluteJoint, used for serialization
	*/
	virtual	const char*			getConcreteTypeName() const { return "PxRevoluteJoint"; }

protected:

	//serialization

	/**
	\brief Constructor
	*/
	PX_INLINE					PxRevoluteJoint(PxType concreteType, PxBaseFlags baseFlags) : PxJoint(concreteType, baseFlags) {}

	/**
	\brief Deserialization constructor
	*/
	PX_INLINE					PxRevoluteJoint(PxBaseFlags baseFlags) : PxJoint(baseFlags) {}

	/**
	\brief Returns whether a given type name matches with the type of this instance
	*/
	virtual	bool				isKindOf(const char* name) const { return !strcmp("PxRevoluteJoint", name) || PxJoint::isKindOf(name); }
	
	//~serialization
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

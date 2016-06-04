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


#ifndef PX_D6JOINT_H
#define PX_D6JOINT_H
/** \addtogroup extensions
  @{
*/

#include "extensions/PxJoint.h"
#include "extensions/PxJointLimit.h"
#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxD6Joint;

/**
\brief Create a D6 joint.

 \param[in] physics the physics SDK
 \param[in] actor0 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame0 the position and orientation of the joint relative to actor0
 \param[in] actor1 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame1 the position and orientation of the joint relative to actor1 

@see PxD6Joint
*/

PxD6Joint*			PxD6JointCreate(PxPhysics& physics, 
									PxRigidActor* actor0, const PxTransform& localFrame0, 
									PxRigidActor* actor1, const PxTransform& localFrame1);




/**
\brief Used to specify one of the degrees of freedom of  a D6 joint.

@see PxD6Joint
*/

struct PxD6Axis
{
	enum Enum
	{
		eX      = 0,	//!< motion along the X axix
		eY      = 1,	//!< motion along the Y axis
		eZ      = 2,	//!< motion along the Z axis
		eTWIST  = 3,	//!< motion around the X axis
		eSWING1 = 4,	//!< motion around the Y axis
		eSWING2 = 5,	//!< motion around the Z axis
		eCOUNT	= 6
	};
};


/**
\brief Used to specify the range of motions allowed for a degree of freedom in a D6 joint.

@see PxD6Joint
*/
struct PxD6Motion
{
	enum Enum
	{
		eLOCKED,	//!< The DOF is locked, it does not allow relative motion.
		eLIMITED,	//!< The DOF is limited, it only allows motion within a specific range.
		eFREE		//!< The DOF is free and has its full range of motion.
	};
};


/**
\brief Used to specify which axes of a D6 joint are driven. 

Each drive is an implicit force-limited damped spring:

force = spring * (target position - position) + damping * (targetVelocity - velocity)

Alternatively, the spring may be configured to generate a specified acceleration instead of a force.

A linear axis is affected by drive only if the corresponding drive flag is set. There are two possible models
for angular drive: swing/twist, which may be used to drive one or more angular degrees of freedom, or slerp,
which may only be used to drive all three angular degrees simultaneously.

@see PxD6Joint
*/

struct PxD6Drive
{
	enum Enum
	{
		eX			= 0,		//!< drive along the X-axis
		eY			= 1,		//!< drive along the Y-axis
		eZ			= 2,		//!< drive along the Z-axis
		eSWING		= 3,		//!< drive of displacement from the X-axis
		eTWIST		= 4,		//!< drive of the displacement around the X-axis
		eSLERP		= 5,		//!< drive of all three angular degrees along a SLERP-path
		eCOUNT		= 6
	};
};

/** 
\brief flags for configuring the drive model of a PxD6Joint

@see PxD6JointDrive PxD6Joint
*/

struct PxD6JointDriveFlag
{
	enum Enum
	{
		eACCELERATION	= 1	//!< drive spring is for the acceleration at the joint (rather than the force) 
	};
};
typedef PxFlags<PxD6JointDriveFlag::Enum, PxU32> PxD6JointDriveFlags;
PX_FLAGS_OPERATORS(PxD6JointDriveFlag::Enum, PxU32)


/** 
\brief parameters for configuring the drive model of a PxD6Joint

@see PxD6Joint
*/

class PxD6JointDrive : public PxSpring
{
//= ATTENTION! =====================================================================================
// Changing the data layout of this class breaks the binary serialization format.  See comments for 
// PX_BINARY_SERIAL_VERSION.  If a modification is required, please adjust the getBinaryMetaData 
// function.  If the modification is made on a custom branch, please change PX_BINARY_SERIAL_VERSION
// accordingly.
//==================================================================================================

public:
	PxReal					forceLimit;			//!< the force limit of the drive - may be an impulse or a force depending on PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES
	PxD6JointDriveFlags		flags;				//!< the joint drive flags 


	/**
	\brief default constructor for PxD6JointDrive.
	*/

	PxD6JointDrive(): PxSpring(0,0), forceLimit(PX_MAX_F32), flags(0) {}

	/**
	\brief constructor a PxD6JointDrive.

	\param[in] driveStiffness the stiffness of the drive spring.
	\param[in] driveDamping the damping of the drive spring
	\param[in] driveForceLimit the maximum impulse or force that can be exerted by the drive
	\param[in] isAcceleration whether the drive is an acceleration drive or a force drive
	*/


	PxD6JointDrive(PxReal driveStiffness, PxReal driveDamping, PxReal driveForceLimit, bool isAcceleration = false)
	: PxSpring(driveStiffness, driveDamping)
	, forceLimit(driveForceLimit)
	, flags(isAcceleration?(PxU32)PxD6JointDriveFlag::eACCELERATION : 0) 
	{}

	/** 
	\brief returns true if the drive is valid
	*/

	bool isValid() const
	{
		return PxIsFinite(stiffness) && stiffness>=0 &&
			   PxIsFinite(damping) && damping >=0 &&
			   PxIsFinite(forceLimit) && forceLimit >=0;
	}
};


/**
 \brief A D6 joint is a general constraint between two actors.
 
 It allows the application to individually define the linear and rotational degrees of freedom, 
 and also to configure a variety of limits and driven degrees of freedom.

 By default all degrees of freedom are locked. So to create a prismatic joint with free motion 
 along the x-axis:

 \code	
    ...
    joint->setMotion(PxD6Axis::eX, PxD6JointMotion::eFREE);
     ...
 \endcode

 Or a Revolute joint with motion free allowed around the x-axis:

 \code
    ... 
	joint->setMotion(PxD6Axis::eTWIST, PxD6JointMotion::eFREE);
    ...
 \endcode

 Degrees of freedom may also be set to limited instead of locked. There is a single limit value
 for all linear degrees of freedom, which may act as a linear, circular, or spherical limit depending
 on which degrees of freedom are limited.

 If the twist degree of freedom is limited, is supports upper and lower limits. The two swing degrees
 of freedom are limited with a cone limit.
@see PxD6JointCreate() PxJoint 
*/

class PxD6Joint : public PxJoint
{
public:

	/**
	\brief Set the motion type around the specified axis.

	Each axis may independently specify that the degree of freedom is locked (blocking relative movement
	along or around this axis), limited by the corresponding limit, or free.

	\param[in] axis the axis around which motion is specified
	\param[in] type the motion type around the specified axis

	<b>Default:</b> all degrees of freedom are locked

	@see getMotion() PxD6Axis PxD6Motion

	*/
	virtual void				setMotion(PxD6Axis::Enum axis, PxD6Motion::Enum type)			= 0;

	/**
	\brief Get the motion type around the specified axis.

	@see setMotion() PxD6Axis PxD6Motion

	\param[in] axis the degree of freedom around which the motion type is specified
	\return the motion type around the specified axis

	*/

	virtual PxD6Motion::Enum	getMotion(PxD6Axis::Enum axis)			const					= 0;

	/**
	\brief get the twist angle of the joint
	*/

	virtual PxReal				getTwist()								const					= 0;

	/**
	\brief get the swing angle of the joint from the Y axis
	*/

	virtual PxReal				getSwingYAngle()						const					= 0;

	/**
	\brief get the swing angle of the joint from the Z axis
	*/

	virtual PxReal				getSwingZAngle()						const					= 0;


	/**
	\brief Set the linear limit for the joint. 

	A single limit constraints all linear limited degrees of freedom, forming a linear, circular 
	or spherical constraint on motion depending on the number of limited degrees.

	\param[in] limit the linear limit structure

	@see getLinearLimit() 
	*/
	virtual	void				setLinearLimit(const PxJointLinearLimit& limit)						= 0;

	/**
	\brief Get the linear limit for the joint. 

	\return the linear limit structure

	@see setLinearLimit() PxJointLinearLimit
	*/

	virtual	PxJointLinearLimit	getLinearLimit()						const					= 0;


	/**
	\brief Set the twist limit for the joint. 

	The twist limit controls the range of motion around the twist axis. 

	The limit angle range is (-2*PI, 2*PI) and the extent of the limit must be strictly less than 2*PI

	\param[in] limit the twist limit structure

	@see getTwistLimit() PxJointAngularLimitPair
	*/
	virtual	void				setTwistLimit(const PxJointAngularLimitPair& limit)				= 0;


	/**
	\brief Get the twist limit for the joint. 

	\return the twist limit structure

	@see setTwistLimit() PxJointAngularLimitPair
	*/
	virtual	PxJointAngularLimitPair	getTwistLimit()							const					= 0;

	/**
	\brief Set the swing cone limit for the joint. 

	\brief The cone limit is used if either or both swing axes are limited. The extents are 
	symmetrical and measured in the frame of the parent. If only one swing degree of freedom 
	is limited, the corresponding value from the cone limit defines the limit range.

	\param[in] limit the cone limit structure

	@see getLimitCone() PxJointLimitCone 
	*/
	virtual	void				setSwingLimit(const PxJointLimitCone& limit)						= 0;

	/**
	\brief Get the cone limit for the joint. 

	\return the swing limit structure

	@see setLimitCone() PxJointLimitCone
	*/
	virtual	PxJointLimitCone	getSwingLimit()							const					= 0;

	/**
	\brief Set the drive parameters for the specified drive type.

	\param[in] index the type of drive being specified
	\param[in] drive the drive parameters

	@see getDrive() PxD6JointDrive

	<b>Default</b> The default drive spring and damping values are zero, the force limit is zero, and no flags are set.

	*/
	virtual void				setDrive(PxD6Drive::Enum index, const PxD6JointDrive& drive)		= 0;

	/**
	\brief Get the drive parameters for the specified drive type. 

	\param[in] index the specified drive type

	@see setDrive() PxD6JointDrive
	*/
	virtual PxD6JointDrive		getDrive(PxD6Drive::Enum index)	const							= 0;

	/**
	\brief Set the drive goal pose 

	The goal is relative to the constraint frame of actor[0]

	<b>Default</b> the identity transform

	\param[in] pose The goal drive pose if positional drive is in use. 

	@see setDrivePosition()
	*/
	virtual void				setDrivePosition(const PxTransform& pose)						= 0;

	/**
	\brief Get the drive goal pose.

	@see getDrivePosition()
	*/

	virtual PxTransform			getDrivePosition()						const					= 0;


	/**
	\brief Set the target goal velocity for drive.

	The velocity is measured in the constraint frame of actor[0]

	\param[in] linear The goal velocity for linear drive
	\param[in] angular The goal velocity for angular drive

	@see getDriveVelocity()
	*/

	virtual	void				setDriveVelocity(const PxVec3& linear,
												 const PxVec3& angular)							= 0;

	/**
	\brief Get the target goal velocity for joint drive.

	\param[in] linear The goal velocity for linear drive
	\param[in] angular The goal velocity for angular drive

	@see setDriveVelocity()
	*/

	virtual void				getDriveVelocity(PxVec3& linear,
												 PxVec3& angular)		const					= 0;
	

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

	\note 
	Angular projection is implemented only for the case of two or three locked angular degrees of freedom.

	@see getProjectionAngularTolerance() PxJoint::setConstraintFlag() PxConstraintFlag::ePROJECTION
	*/

	virtual void				setProjectionAngularTolerance(PxReal tolerance)							= 0;

	/**
	\brief Get the angular tolerance threshold for projection.

	\return tolerance the angular tolerance threshold in radians

	@see setProjectionAngularTolerance()
	*/

	virtual PxReal				getProjectionAngularTolerance()			const					= 0;

	/**
	\brief Returns string name of PxD6Joint, used for serialization
	*/
	virtual	const char*			getConcreteTypeName() const { return "PxD6Joint"; }


protected:

	//serialization

	/**
	\brief Constructor
	*/
	PX_INLINE					PxD6Joint(PxType concreteType, PxBaseFlags baseFlags) : PxJoint(concreteType, baseFlags) {}

	/**
	\brief Deserialization constructor
	*/
	PX_INLINE					PxD6Joint(PxBaseFlags baseFlags) : PxJoint(baseFlags) {}

	/**
	\brief Returns whether a given type name matches with the type of this instance
	*/
	virtual	bool				isKindOf(const char* name) const { return !strcmp("PxD6Joint", name) || PxJoint::isKindOf(name); }

	//~serialization
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

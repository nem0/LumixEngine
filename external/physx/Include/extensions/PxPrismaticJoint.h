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


#ifndef PX_PRISMATICJOINT_H
#define PX_PRISMATICJOINT_H
/** \addtogroup extensions
  @{
*/

#include "extensions/PxJoint.h"
#include "extensions/PxJointLimit.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPrismaticJoint;

/**
\brief Create a prismatic joint.

 \param[in] physics the physics SDK
 \param[in] actor0 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame0 the position and orientation of the joint relative to actor0
 \param[in] actor1 an actor to which the joint is attached. NULL may be used to attach the joint to a specific point in the world frame
 \param[in] localFrame1 the position and orientation of the joint relative to actor1 

@see PxPrismaticJoint
*/

PxPrismaticJoint*	PxPrismaticJointCreate(PxPhysics& physics, 
										   PxRigidActor* actor0, const PxTransform& localFrame0, 
										   PxRigidActor* actor1, const PxTransform& localFrame1);


/**
\brief Flags specific to the prismatic joint.

@see PxPrismaticJoint
*/

struct PxPrismaticJointFlag
{
	enum Enum
	{
		eLIMIT_ENABLED	= 1<<1,
	};
};

typedef PxFlags<PxPrismaticJointFlag::Enum, PxU16> PxPrismaticJointFlags;
PX_FLAGS_OPERATORS(PxPrismaticJointFlag::Enum, PxU16);


/**
 \brief A prismatic joint permits relative translational movement between two bodies along
 an axis, but no relative rotational movement.

 the axis on each body is defined as the line containing the origin of the joint frame and
 extending along the x-axis of that frame

 \image html prismJoint.png

 @see PxPrismaticJointCreate() PxJoint
*/

class PxPrismaticJoint: public PxJoint
{
public:
	static const PxJointType::Enum Type = PxJointType::ePRISMATIC;


	/**
	\brief sets the joint upper limit  parameters.

	@see PxJointLimit getLowerLimit()
	*/
	virtual void			setLimit(const PxJointLimitPair&)			= 0;

	/**
	\brief gets the joint upper limit  parameters.

	@see PxJointLimit getLowerLimit()
	*/
	virtual PxJointLimitPair getLimit()					const			= 0;


	/**
	\brief Set the flags specific to the Prismatic Joint.

	<b>Default</b> PxPrismaticJointFlags(0)

	\param[in] flags The joint flags.

	@see PxPrismaticJointFlag setFlag() getFlags()
	*/

	virtual void					setPrismaticJointFlags(PxPrismaticJointFlags flags) = 0;

	/**
	\brief Set a single flag specific to a Prismatic Joint to true or false.

	\param[in] flag The flag to set or clear.
	\param[in] value the value to which to set the flag

	@see PxPrismaticJointFlag, getFlags() setFlags()
	*/

	virtual void					setPrismaticJointFlag(PxPrismaticJointFlag::Enum flag, bool value) = 0;

	/**
	\brief Get the flags specific to the Prismatic Joint.

	\return the joint flags

	@see PxPrismaticJoint::flags, PxPrismaticJointFlag setFlag() setFlags()
	*/

	virtual PxPrismaticJointFlags	getPrismaticJointFlags(void)					const	= 0;

		/**
	\brief Set the linear tolerance threshold for projection.

	If the joint separates by more than this distance along its locked degrees of freedom, the solver 
	will move the bodies to close the distance. 

	Setting a very small tolerance may result in simulation jitter or other artifacts.

	Sometimes it is not possible to project (for example when the joints form a cycle).

	This value must be nonnegative.

	<b>Range:</b> [0,inf)<br>
	<b>Default:</b> 1e10f

	\param[in] tolerance the linear tolerance threshold

	@see getProjectionLinearTolerance()
	*/
	virtual void			setProjectionLinearTolerance(PxReal tolerance)					= 0;

	/**
	\brief Get the linear tolerance threshold for projection.

	\return the linear tolerance threshold in radians

	@see setProjectionLinearTolerance()
	*/

	virtual PxReal			getProjectionLinearTolerance()			const					= 0;

	/**
	\brief Set the angular tolerance threshold for projection. Projection is enabled if PxConstraintFlag::ePROJECTION
	is set for the joint.

	If the joint separates by more than this distance along its locked degrees of freedom, the solver 
	will move the bodies to close the distance.

	Setting a very small tolerance may result in simulation jitter or other artifacts.

	Sometimes it is not possible to project (for example when the joints form a cycle).

	<b>Range:</b> [0,inf)<br>
	<b>Default:</b> 1e10f

	\param[in] tolerance the linear tolerance threshold

	@see getProjectionLinearTolerance() PxJoint::setConstraintFlags()
	*/

	virtual void			setProjectionAngularTolerance(PxReal tolerance)					= 0;

	/**
	\brief Get the angular tolerance threshold for projection.

	@see getProjectionAngularTolerance()
	*/
	virtual PxReal			getProjectionAngularTolerance()			const					= 0;


	virtual	const char*		getConcreteTypeName() const				{	return "PxPrismaticJoint"; }

protected:
	PxPrismaticJoint(PxRefResolver& v)	: PxJoint(v)	{}
	PxPrismaticJoint()									{}
	virtual	bool			isKindOf(const char* name)	const		{	return !strcmp("PxPrismaticJoint", name) || PxJoint::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

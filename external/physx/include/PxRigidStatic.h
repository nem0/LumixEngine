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


#ifndef PX_PHYSICS_NX_RIGIDSTATIC
#define PX_PHYSICS_NX_RIGIDSTATIC
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "PxRigidActor.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief PxRigidStatic represents a static rigid body simulation object in the physics SDK.

PxRigidStatic objects are static rigid physics entities. They shall be used to define solid objects which are fixed in the world.

<h3>Creation</h3>
Instances of this class are created by calling #PxPhysics::createRigidStatic() and deleted with #release().

<h3>Visualizations</h3>
\li #PxVisualizationParameter::eACTOR_AXES

@see PxRigidActor  PxPhysics.createRigidStatic()  release()
*/

class PxRigidStatic : public PxRigidActor
{
public:
	virtual		const char*		getConcreteTypeName() const { return "PxRigidStatic"; }

protected:
	PX_INLINE					PxRigidStatic(PxType concreteType, PxBaseFlags baseFlags) : PxRigidActor(concreteType, baseFlags) {}
	PX_INLINE					PxRigidStatic(PxBaseFlags baseFlags) : PxRigidActor(baseFlags) {}
	virtual						~PxRigidStatic() {}
	virtual		bool			isKindOf(const char* name)	const { return !strcmp("PxRigidStatic", name) || PxRigidActor::isKindOf(name); }

};

PX_DEPRECATED PX_INLINE	PxRigidStatic*			PxActor::isRigidStatic()			{ 	return is<PxRigidStatic>();	}
PX_DEPRECATED PX_INLINE	const PxRigidStatic*	PxActor::isRigidStatic() const		{	return is<PxRigidStatic>();	}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

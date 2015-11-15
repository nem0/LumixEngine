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


#ifndef PX_PHYSICS_PX_PARTICLESYSTEM
#define PX_PHYSICS_PX_PARTICLESYSTEM
/** \addtogroup particles
  @{
*/

#include "particles/PxParticleBase.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief The particle system class represents the main module for particle based simulation.

This class inherits the properties of the PxParticleBase class.

The particle system class manages a set of particles.  Particles can be created, released and updated directly through the API.
When a particle is created the user gets an id for it which can be used to address the particle until it is released again.

Particles collide with static and dynamic shapes.  They are also affected by the scene gravity and a user force, 
as well as global velocity damping.  When a particle collides, a particle flag is raised corresponding to the type of 
actor, static or dynamic, it collided with.  Additionally a shape can be flagged as a drain (See PxShapeFlag), in order to get a corresponding 
particle flag raised when a collision occurs.  This information can be used to delete particles.

The particles of a particle system don't collide with each other.  In order to simulate particle-particle interactions use the 
subclass PxParticleFluid.

@see PxParticleBase, PxParticleReadData, PxPhysics.createParticleSystem
*/
class PxParticleSystem : public PxParticleBase
{
public:

		virtual		const char*					getConcreteTypeName() const { return "PxParticleSystem"; }

/************************************************************************************************/

protected:
	PX_INLINE									PxParticleSystem(PxType concreteType, PxBaseFlags baseFlags) : PxParticleBase(concreteType, baseFlags) {}
	PX_INLINE									PxParticleSystem(PxBaseFlags baseFlags) : PxParticleBase(baseFlags) {}
	virtual										~PxParticleSystem() {}
	virtual			bool						isKindOf(const char* name) const { return !strcmp("PxParticleSystem", name) || PxParticleBase::isKindOf(name);  }

};

PX_DEPRECATED PX_INLINE PxParticleSystem*		PxActor::isParticleSystem()				{ return is<PxParticleSystem>();	}
PX_DEPRECATED PX_INLINE const PxParticleSystem*	PxActor::isParticleSystem()		const	{ return is<PxParticleSystem>();	}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

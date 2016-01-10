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

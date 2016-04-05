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


#ifndef PX_PARTICLE_BASE_FLAG
#define PX_PARTICLE_BASE_FLAG
/** \addtogroup particles
  @{
*/

#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief ParticleBase flags
*/
struct PxParticleBaseFlag
{
	enum Enum
	{
		/**
		\brief Enable/disable two way collision of particles with the rigid body scene.
		In either case, particles are influenced by colliding rigid bodies.
		If eCOLLISION_TWOWAY is not set, rigid bodies are not influenced by 
		colliding particles. Use particleMass to
		control the strength of the feedback force on rigid bodies.
		
		\note Switching this flag while the particle system is part of a scene might have a negative impact on performance.
		*/
		eCOLLISION_TWOWAY					= (1<<0),

		/**
		\brief Enable/disable collision of particles with dynamic actors.
		The flag can be turned off as a hint to the sdk to save memory space and 
		execution time. In principle any collisions can be turned off using filters
		but without or reduced memory and performance benefits.

		\note Switching this flag while the particle system is part of a scene might have a negative impact on performance.
		*/
		eCOLLISION_WITH_DYNAMIC_ACTORS		= (1<<1),

		/**
		\brief Enable/disable execution of particle simulation.
		*/
		eENABLED							= (1<<2),

		/**
		\brief Defines whether the particles of this particle system should be projected to a plane.
		This can be used to build 2D applications, for instance. The projection
		plane is defined by the parameter projectionPlaneNormal and projectionPlaneDistance.
		*/
		ePROJECT_TO_PLANE					= (1<<3),

		/**
		\brief Enable/disable per particle rest offsets.
		Per particle rest offsets can be used to support particles having different sizes with 
		respect to collision.
		
		\note This configuration cannot be changed after the particle system was created.
		*/
		ePER_PARTICLE_REST_OFFSET			= (1<<4),

		/**
		\brief Ename/disable per particle collision caches.
		Per particle collision caches improve collision detection performance at the cost of increased 
		memory usage.

		\note Switching this flag while the particle system is part of a scene might have a negative impact on performance.
		*/
		ePER_PARTICLE_COLLISION_CACHE_HINT	= (1<<5),

		/**
		\brief Enable/disable GPU acceleration. 
		Enabling GPU acceleration might fail. In this case the eGPU flag is switched off. 

		\note Switching this flag while the particle system is part of a scene might have a negative impact on performance.
		
		@see PxScene.removeActor() PxScene.addActor() PxParticleGpu
		*/
		eGPU								= (1<<6)
	};
};


/**
\brief collection of set bits defined in PxParticleBaseFlag.

@see PxParticleBaseFlag
*/
typedef PxFlags<PxParticleBaseFlag::Enum,PxU16> PxParticleBaseFlags;
PX_FLAGS_OPERATORS(PxParticleBaseFlag::Enum,PxU16)

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

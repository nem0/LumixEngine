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


#ifndef PX_PARTICLE_FLAG
#define PX_PARTICLE_FLAG
/** \addtogroup particles
  @{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
Particle flags are used for additional information on the particles.
*/
struct PxParticleFlag
	{
	enum Enum
		{
			/**
			\brief Marks a valid particle. The particle data corresponding to these particle flags is valid, i.e. defines a particle, when set.
			Particles that are not marked with PxParticleFlag::eVALID are ignored during simulation.
			
			Application read only.
			*/
			eVALID								= (1<<0),

			/**
			\brief Marks a particle that has collided with a static actor shape.

			Application read only.
			*/
			eCOLLISION_WITH_STATIC				= (1<<1),	

			/**
			\brief Marks a particle that has collided with a dynamic actor shape.

			Application read only.
			*/
			eCOLLISION_WITH_DYNAMIC				= (1<<2),

			/**
			\brief Marks a particle that has collided with a shape that has been flagged as a drain (See PxShapeFlag.ePARTICLE_DRAIN).
			
			Application read only.
			@see PxShapeFlag.ePARTICLE_DRAIN
			*/
			eCOLLISION_WITH_DRAIN				= (1<<3),

			/**
			\brief Marks a particle that had to be ignored for simulation, because the spatial data structure ran out of resources.

			Application read only.
			*/
			eSPATIAL_DATA_STRUCTURE_OVERFLOW	= (1<<4)
		};
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

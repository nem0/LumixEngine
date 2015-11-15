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

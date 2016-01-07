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


#ifndef PX_PHYSICS_NX_CLOTH_READ_DATA
#define PX_PHYSICS_NX_CLOTH_READ_DATA

#include "PxPhysXConfig.h"

#if PX_USE_CLOTH_API

/** \addtogroup cloth
  @{
*/

#include "PxLockedData.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

struct PxClothParticle;

/**
\brief Data layout descriptor for reading cloth data from the SDK.
@see PxCloth.lockParticleData()
*/
class PxClothParticleData : public PxLockedData
{
public:
	/**
	\brief Particle position and mass data.
	@see PxCloth.getNbParticles()
	*/
	PxClothParticle* particles;

	/**
	\brief Particle position and mass data from the second last iteration.
	\details Can be used together with #particles and #PxCloth.getPreviousTimeStep() to compute particle velocities.
	*/
	PxClothParticle* previousParticles;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */

#endif // PX_USE_CLOTH_API

#endif

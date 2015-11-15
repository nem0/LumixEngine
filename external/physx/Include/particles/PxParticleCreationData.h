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


#ifndef PX_PARTICLESYSTEM_NXPARTICLECREATIONDATA
#define PX_PARTICLESYSTEM_NXPARTICLECREATIONDATA
/** \addtogroup particles
@{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxVec3.h"
#include "foundation/PxStrideIterator.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Descriptor-like user-side class describing buffers for particle creation.

PxParticleCreationData is used to create particles within the SDK. The SDK copies the particle data referenced by PxParticleCreationData, it
may therefore be deallocated right after the creation call returned.

@see PxParticleBase::createParticles()
*/
class PxParticleCreationData
{
public:

	/**
	\brief The number of particles stored in the buffer. 
	*/
	PxU32									numParticles;

	/**
	\brief Particle index data.

	When creating particles, providing the particle indices is mandatory.
	*/
	PxStrideIterator<const PxU32>			indexBuffer;

	/**
	\brief Particle position data.

	When creating particles, providing the particle positions is mandatory.
	*/
	PxStrideIterator<const PxVec3>			positionBuffer;

	/**
	\brief Particle velocity data.

	Providing velocity data is optional.
	*/
	PxStrideIterator<const PxVec3>			velocityBuffer;

	/**
	\brief Particle rest offset data. 

	Values need to be in the range [0.0f, restOffset].
	If PxParticleBaseFlag.ePER_PARTICLE_REST_OFFSET is set, providing per particle rest offset data is mandatory.  
	@see PxParticleBaseFlag.ePER_PARTICLE_REST_OFFSET.
	*/
	PxStrideIterator<const PxF32>			restOffsetBuffer;

	/**
	\brief Particle flags.
	
	PxParticleFlag.eVALID, PxParticleFlag.eCOLLISION_WITH_STATIC, PxParticleFlag.eCOLLISION_WITH_DYNAMIC,
	PxParticleFlag.eCOLLISION_WITH_DRAIN, PxParticleFlag.eSPATIAL_DATA_STRUCTURE_OVERFLOW are all flags that 
	can't be set on particle creation. They are written by the SDK exclusively.
	
	Providing flag data is optional.
	@see PxParticleFlag
	*/
	PxStrideIterator<const PxU32>			flagBuffer;


	PX_INLINE ~PxParticleCreationData();
	
	/**
	\brief (Re)sets the structure to the default.	
	*/
	PX_INLINE void setToDefault();
	
	/**
	\brief Returns true if the current settings are valid
	*/
	PX_INLINE bool isValid() const;

	/**
	\brief Constructor sets to default.
	*/
	PX_INLINE	PxParticleCreationData();
};

PX_INLINE PxParticleCreationData::PxParticleCreationData()
{
	indexBuffer					= PxStrideIterator<const PxU32>();
	positionBuffer				= PxStrideIterator<const PxVec3>();
	velocityBuffer				= PxStrideIterator<const PxVec3>();
	restOffsetBuffer			= PxStrideIterator<const PxF32>();
	flagBuffer					= PxStrideIterator<const PxU32>();
}

PX_INLINE PxParticleCreationData::~PxParticleCreationData()
{

}

PX_INLINE void PxParticleCreationData::setToDefault()
{
	*this = PxParticleCreationData();
}

PX_INLINE bool PxParticleCreationData::isValid() const
{
	if (numParticles > 0 && !(indexBuffer.ptr() && positionBuffer.ptr())) return false;
	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

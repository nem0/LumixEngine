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


#ifndef PX_PARTICLESYSTEM_NXPARTICLEREADDATA
#define PX_PARTICLESYSTEM_NXPARTICLEREADDATA
/** \addtogroup particles
  @{
*/

#include "foundation/PxStrideIterator.h"
#include "PxLockedData.h"
#include "foundation/PxFlags.h"
#include "PxParticleFlag.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
Flags to configure PxParticleBase simulation output that can be read by the application. 
Disabling unneeded buffers saves memory and improves performance.
*/
struct PxParticleReadDataFlag
	{
	enum Enum
		{
			/**
			Enables reading particle positions from the SDK.
			@see PxParticleReadData.positionBuffer
			*/
			ePOSITION_BUFFER			= (1<<0),

			/**
			Enables reading particle velocities from the SDK.
			@see PxParticleReadData.velocityBuffer
			*/
			eVELOCITY_BUFFER			= (1<<1),

			/**
			Enables reading per particle rest offsets from the SDK.
			Per particle rest offsets are never changed by the simulation.
			This option may only be used on particle systems that have 
			PxParticleBaseFlag.ePER_PARTICLE_REST_OFFSET enabled.
			@see PxParticleBaseFlag.ePER_PARTICLE_REST_OFFSET
			*/
			eREST_OFFSET_BUFFER			= (1<<2),

			/**
			Enables reading particle flags from the SDK.
			@see PxParticleReadData.flagsBuffer
			*/
			eFLAGS_BUFFER				= (1<<3),

			/**
			Enables reading particle collision normals from the SDK.
			@see PxParticleReadData.collisionNormalBuffer
			*/
			eCOLLISION_NORMAL_BUFFER	= (1<<4),

			/**
			Enables reading particle collision velocities from the SDK.
			@see PxParticleReadData.collisionVelocity
			*/
			eCOLLISION_VELOCITY_BUFFER	= (1<<5),

			/**
			Enables reading particle densities from the SDK. (PxParticleFluid only)
			@see PxParticleFluidReadData.densityBuffer
			*/
			eDENSITY_BUFFER				= (1<<6)
		};
	};

/**
\brief collection of set bits defined in PxParticleReadDataFlag.

@see PxParticleReadDataFlag
*/
typedef PxFlags<PxParticleReadDataFlag::Enum,PxU16> PxParticleReadDataFlags;
PX_FLAGS_OPERATORS(PxParticleReadDataFlag::Enum,PxU16)

/**
\brief collection of set bits defined in PxParticleFlag.

@see PxParticleFlag
*/
typedef PxFlags<PxParticleFlag::Enum,PxU16> PxParticleFlags;
PX_FLAGS_OPERATORS(PxParticleFlag::Enum,PxU16)

/**
\brief Data layout descriptor for reading particle data from the SDK.

PxParticleReadData is used to retrieve information about simulated particles. It can be accessed by calling PxParticleBase.lockParticleReadData().

Each particle is described by its position, velocity, a set of (PxParticleFlag) flags and information on collisions (collision normal).
The particle buffers are sparse, i.e. occupied particle indices will have PxParticleFlag.eVALID set in the corresponding entry of 
PxParticleReadData.flagsBuffer. Alternatively valid particles can be identified with the bitmap PxParticleReadData.validParticleBitmap.
If (and only if) the index range of valid particles PxParticleReadData.validParticleRange is greater 0, i.e. any particles are present, 
data can be read from the particle buffers. Additionally individual particle buffers can only be read if the corresponding 
PxParticleReadDataFlag in particleReadDataFlags is set.

The particle data buffer are defined in the range [0, PxParticleReadData.validParticleRange-1].
The bitmap words are defined in the range [0, (PxParticleReadData.validParticleRange-1) >> 5].

@see PxParticleBase::lockParticleReadData()
*/
class PxParticleReadData : public PxLockedData
	{
	public:

	/**
	\brief Number of particles (only including particles with PxParticleFlag.eVALID set). 
	*/
	PxU32										nbValidParticles;

	/**
	\brief Index after the last valid particle (PxParticleFlag.eVALID set). Its 0 if there are no valid particles. 
	*/
	PxU32										validParticleRange;

	/**
	\brief Bitmap marking valid particle indices. The bitmap is defined between [0, (PxParticleReadData.validParticleRange-1) >> 5].
	\note Might be NULL if PxParticleReadData.validParticleRange == 0. 
	*/
	const PxU32*								validParticleBitmap;

	/**
	\brief Particle position data.
	*/
	PxStrideIterator<const PxVec3>				positionBuffer;
	
	/**
	\brief Particle velocity data.
	*/
	PxStrideIterator<const PxVec3>				velocityBuffer;

	/**
	\brief Particle rest offset data.
	*/
	PxStrideIterator<const PxF32>				restOffsetBuffer;

	/**
	\brief Particle flags.
	*/
	PxStrideIterator<const PxParticleFlags>		flagsBuffer;

	/**
	\brief Collision normals of colliding particles.
	The collision normal buffer is only guaranteed to be valid after the particle 
	system has been simulated. Otherwise collisionNormalBuffer.ptr() is NULL. This also 
	applies to particle systems that are not assigned to a scene.
	*/
	PxStrideIterator<const PxVec3>				collisionNormalBuffer;
	
	/**
	\brief Velocities of particles relative to shapes they collide with.
	The collision velocity buffer is only guaranteed to be valid after the particle 
	system has been simulated. Otherwise collisionVelocityBuffer.ptr() is NULL. This also 
	applies to particle systems that are not assigned to a scene.
	The collision velocity is identical to the particle velocity if the particle is not colliding.
	*/
	PxStrideIterator<const PxVec3>				collisionVelocityBuffer;

	/**
	\brief Returns PxDataAccessFlag::eREADABLE, since PxParticleReadData is read only data
	@see PxLockedData
	*/
    virtual PxDataAccessFlags getDataAccessFlags() = 0;

	/**
	\brief Unlocks the data.
	@see PxLockedData
	*/
    virtual void unlock() = 0;

	/**
	\brief virtual destructor
	*/
	virtual ~PxParticleReadData() {}

	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

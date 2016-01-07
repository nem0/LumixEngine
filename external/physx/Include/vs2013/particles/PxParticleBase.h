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


#ifndef PX_PHYSICS_PX_PARTICLEBASE
#define PX_PHYSICS_PX_PARTICLEBASE
/** \addtogroup particles
  @{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxBounds3.h"
#include "PxFiltering.h"
#include "particles/PxParticleBaseFlag.h"
#include "PxActor.h"
#include "particles/PxParticleCreationData.h"
#include "particles/PxParticleReadData.h"
#include "PxForceMode.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief The particle base class represents the shared module for particle based simulation. This class can't be instantiated.

The particle base class manages a set of particles.  Particles can be created, released and updated directly through the API.
When a particle is created the user gets an index for it which can be used to address the particle until it is released again.

Particles collide with static and dynamic shapes.  They are also affected by the scene gravity and a user force, 
as well as global velocity damping.  When a particle collides, a particle flag is raised corresponding to the type of 
actor, static or dynamic, it collided with.  Additionally a shape can be flagged as a drain (See PxShapeFlag), in order to get a corresponding 
particle flag raised when a collision occurs.  This information can be used to delete particles.

@see PxParticleCreationData, PxParticleReadData, PxShapeFlag, PxParticleSystem, PxParticleFluid
*/
class PxParticleBase : public PxActor
{

	public:

/************************************************************************************************/

/** @name Particle Access and Manipulation
*/
//@{

	/**
	\brief Locks the particle data and provides the data descriptor for accessing the particles.
	After reading from the buffers the application needs to call PxParticleReadData::unlock() before any 
	SDK operation can access the buffers. Particularly the buffers need to be unlocked before calling 
	PxParticleBase::lockParticleReadData(), PxParticleBase::createParticles(), PxParticleBase::releaseParticles(),
	PxScene::fetchResults().

	\param flags If PxDataAccessFlag::eDEVICE is specified for GPU particles then pointers to GPU memory will be returned otherwise it will be ignored.
	\note PxDataAccessFlag::eWRITEABLE is not supported and will be ignored
	\note If using PxDataAccessFlag::eDEVICE, newly created particles will not become visible in the GPU buffers until a subsequent simulation step has completed
	@see PxParticleReadData
	*/
	virtual		PxParticleReadData*			lockParticleReadData(PxDataAccessFlags flags) = 0;

	/** 
	\brief Locks the particle read data and provides the data descriptor for accessing the particles
	\note This method does the same as lockParticleReadData(PxDataAccessFlags::eREADABLE)
	@see PxParticleReadData
	*/
	virtual		PxParticleReadData*			lockParticleReadData() = 0;

	/**
	\brief Creates new particles.
	
	The PxParticleCreationData descriptor is used to create new particles based on the provided PxParticleCreationData. 
	Providing particle indices and positions is mandatory.  Indices need to be consistent with the available particle slots within 
	the range [0, maxParticles-1].  The new particles can be immediately read from the application readable 
	particle data, PxParticleReadData. 
	
	\param creationData specifies particle attributes for the particles to be created. (all buffers set have to be consistent with numParticles). 
	\return whether the operation was successful.

	@see PxParticleCreationData, PxParticleReadData, PxParticlesExt.IndexPool
	*/
	virtual		bool						createParticles(const PxParticleCreationData& creationData)										= 0;

	/**
	\brief Releases particles.
	
	Particles corresponding to passed indices are released. Releasing a particle will immediately mark the particle in the 
	application readable particle data, PxParticleReadData, as being invalid, removing PxParticleFlag::eVALID.
	Passing duplicate indices is not allowed.

	\param numParticles Number of particles to be released.
	\param indexBuffer Structure describing indices of particles that should be deleted. (Has to be consistent with numParticles).

	@see PxParticleReadData
	*/
	virtual		void						releaseParticles(PxU32 numParticles, const PxStrideIterator<const PxU32>& indexBuffer)					= 0;

	/**
	\brief Releases all particles.

	Application readable particle data is updated accordingly.
	*/
	virtual		void						releaseParticles()																						= 0;

	/**
	\brief Sets particle positions.

	Directly sets the positions of particles. The supplied positions are used to change particles in the order of
	the indices listed in the index buffer. Duplicate indices are allowed. A position buffer of stride zero is allowed.
	Application readable particle data is updated accordingly.
	
	\param numParticles Number of particle updates.
	\param indexBuffer Structure describing indices of particles that should be updated. (Has to be consistent with numParticles).
	\param positionBuffer Structure describing positions for position updates. (Has to be consistent with numParticles).
	*/
	virtual		void						setPositions(PxU32 numParticles, const PxStrideIterator<const PxU32>& indexBuffer,
														 const PxStrideIterator<const PxVec3>& positionBuffer)										= 0;

	/**
	\brief Sets particle velocities.

	Directly sets the velocities of particles. The supplied velocities are used to change particles in the order of
	the indices listed in the index buffer. Duplicate indices are allowed. A velocity buffer of stride zero is allowed.
	Application readable particle data is updated accordingly.
	
	\param numParticles Number of particle updates.
	\param indexBuffer Structure describing indices of particles that should be updated. (Has to be consistent with numParticles).
	\param velocityBuffer Structure describing velocities for velocity updates. (Has to be consistent with numParticles).
	*/
	virtual		void						setVelocities(PxU32 numParticles, const PxStrideIterator<const PxU32>& indexBuffer,
														  const PxStrideIterator<const PxVec3>& velocityBuffer)										= 0;

	/**
	\brief Sets particle rest offsets.

	Directly sets the rest offsets of particles. The supplied rest offsets are used to change particles in the order of
	the indices listed in the index buffer. The provided offsets need to be in the range [0.0f, restOffset].
	Duplicate indices are allowed. A rest offset buffer of stride zero is allowed.
	Application readable particle data is updated accordingly.
	
	\param numParticles Number of particle updates.
	\param indexBuffer Structure describing indices of particles that should be updated. (Has to be consistent with numParticles).
	\param restOffsetBuffer Structure describing rest offsets for rest offset updates. (Has to be consistent with numParticles).
	
	@see PxParticleBaseFlag.ePER_PARTICLE_REST_OFFSET
	*/
	virtual		void						setRestOffsets(PxU32 numParticles, const PxStrideIterator<const PxU32>& indexBuffer,
														   const PxStrideIterator<const PxF32>& restOffsetBuffer)									= 0;


	/**
	\brief Set forces to be applied to the particles when the simulation starts.

	This call is ignored on particle system that aren't assigned to a scene.
	
	\param numParticles Number of particle updates.
	\param indexBuffer Structure describing indices of particles that should be updated. (Has to be consistent with numParticles).
	\param forceBuffer Structure describing values for particle updates depending on forceMode. (Has to be consistent with numParticles).
	\param forceMode Describes type of update.
	*/
	virtual		void						addForces(PxU32 numParticles, const PxStrideIterator<const PxU32>& indexBuffer,
													  const PxStrideIterator<const PxVec3>& forceBuffer, PxForceMode::Enum forceMode)				= 0;

//@}
/************************************************************************************************/

/** @name ParticleBase Parameters
*/
//@{

	/**
	\brief Returns the particle system damping.

	\return The particle system damping.
	*/
	virtual		PxReal						getDamping()												const	= 0;

	/**
	\brief Sets the particle system damping (must be nonnegative).

	\param damp The new particle system damping.
	*/
	virtual		void 						setDamping(PxReal damp)												= 0;

	/**
	\brief Returns the external acceleration applied to each particle at each time step.

	\return The external acceleration applied to particles.
	*/
	virtual		PxVec3						getExternalAcceleration()									const	= 0;

	/**
	\brief Sets the external acceleration applied to each particle at each time step.

	\param acceleration External acceleration to apply to particles.

	@see getExternalAcceleration()
	*/
	virtual		void 						setExternalAcceleration(const PxVec3&acceleration)					= 0;

	/**
	\brief Returns the plane the particles are projected to.

	\param[out] normal Particle projection plane normal
	\param[out] distance Particle projection plane constant term
	*/
	virtual		void						getProjectionPlane(PxVec3& normal, PxReal& distance)		const	= 0;

	/**
	\brief Sets the plane the particles are projected to.

	Points p on the plane have to fulfill the equation:
	
	(normal.x * p.x)  +  (normal.y * p.y)  +  (normal.z * p.z)  +  d = 0

	\param[in] normal Particle projection plane normal
	\param[in] distance Particle projection plane constant term
	*/
	virtual		void 						setProjectionPlane(const PxVec3& normal, PxReal distance)			= 0;
//@}
/************************************************************************************************/

/** @name Collisions
*/
//@{

	/**
	\brief Returns the mass of a particle. 

	\return Particle mass.
	*/
	virtual		PxReal						getParticleMass()											const	= 0;

	/**
	\brief Sets the mass of a particle. 

	\param mass The particle mass.
	*/
	virtual		void						setParticleMass(PxReal mass)										= 0;

	/**
	\brief Returns the restitution used for collision with shapes.

	\return The restitution.
	*/
	virtual		PxReal						getRestitution()											const	= 0;

	/**
	\brief Sets the restitution used for collision with shapes.
	
	Must be between 0 and 1.

	\param rest The new restitution.
	*/
	virtual		void 						setRestitution(PxReal rest)											= 0;

	/**
	\brief Returns the dynamic friction used for collision with shapes.

	\return The dynamic friction.
	*/
	virtual		PxReal						getDynamicFriction()										const	= 0;

	/**
	\brief Sets the dynamic friction used for collision with shapes.
	
	Must be between 0 and 1.

	\param friction The new dynamic friction
	*/
	virtual		void 						setDynamicFriction(PxReal friction)									= 0;

	/**
	\brief Returns the static friction used for collision with shapes.

	\return The static friction.
	*/
	virtual		PxReal						getStaticFriction()											const	= 0;

	/**
	\brief Sets the static friction used for collision with shapes.
	
	Must be non-negative.
	
	\param friction The new static friction
	*/
	virtual		void 						setStaticFriction(PxReal friction)									= 0;

//@}
/************************************************************************************************/

/** @name Collision Filtering
*/
//@{

	/**
	\brief Sets the user definable collision filter data.

	@see getSimulationFilterData()
	*/
	virtual		void						setSimulationFilterData(const PxFilterData& data)					= 0;

	/**
	\brief Retrieves the object's collision filter data.

	@see setSimulationFilterData()
	*/
	virtual		PxFilterData				getSimulationFilterData()									const	= 0;

	/**
	\deprecated
	\brief Marks the object to reset interactions and re-run collision filters in the next simulation step.
	
	\note This method has been deprecated. Please use #PxScene::resetFiltering() instead.
	*/
	PX_DEPRECATED virtual void				resetFiltering() = 0;

//@}
/************************************************************************************************/

	/**
	\brief Sets particle system flags.

	\param flag Member of #PxParticleBaseFlag.
	\param val New flag value.
	*/
	virtual		void						setParticleBaseFlag(PxParticleBaseFlag::Enum flag, bool val)	= 0;

	/**
	\brief Returns particle system flags.

	\return The current flag values.
	*/
	virtual		PxParticleBaseFlags			getParticleBaseFlags()									const	= 0;

/************************************************************************************************/

/** @name ParticleSystem Property Read Back
*/
//@{

	/**
	\brief Returns the maximum number of particles for this particle system.

	\return Max number of particles for this particle system.
	*/
	virtual		PxU32 						getMaxParticles()										const	= 0;

	/**
	\brief Returns the maximal motion distance (the particle can move the maximal distance of 
	getMaxMotionDistance() during one timestep).

	\return maximum motion distance.
	*/
	virtual		PxReal						getMaxMotionDistance()									const	= 0;

	/**
	\brief Sets the maximal motion distance (the particle can move the maximal distance 
	 during one timestep). Immutable when the particle system is part of a scene.

	\param distance New Max motionDistance value.
	*/
	virtual		void						setMaxMotionDistance(PxReal distance)							= 0;

	/**
	\brief Returns the distance between particles and collision geometry, which is maintained during simulation.

	\return rest offset.
	*/
	virtual		PxReal						getRestOffset()											const	= 0;

	/**
	\brief Sets the distance between particles and collision geometry, which is maintained during simulation.
	If per particle restOffsets are used, they need to be in the range [0.0f, restOffset]. Immutable when the
	particle system is part of a scene.
	\param restOffset New restOffset value.
	*/
	virtual		void						setRestOffset(PxReal restOffset)								= 0;

	/**
	\brief Returns the distance at which contacts are generated between particles and collision geometry.

	\return contact offset.
	*/
	virtual		PxReal						getContactOffset()										const	= 0;

	/**
	\brief Sets the distance at which contacts are generated between particles and collision geometry.
	Immutable when the particle system is part of a scene.

	\param contactOffset New contactOffset value.
	*/
	virtual		void						setContactOffset(PxReal contactOffset)							= 0;

	/**
	\brief Returns the particle grid size used for internal spatial data structures.

	The actual grid size used might differ from the grid size set in the setGridSize(). 

	\return The grid size.
	*/
	virtual		PxReal						getGridSize()											const	= 0;

	/**
	\brief Sets the particle grid size used for internal spatial data structures.
	Immutable when the particle system is part of a scene.
	The actual grid size used might differ from the grid size set in the setGridSize(). 

	\param gridSize New gridSize value.
	*/
	virtual		void						setGridSize(PxReal gridSize)									= 0;

	/**
	\brief Returns particle read data flags.
	\return The particle read data flags.
	@see PxParticleReadDataFlags
	*/
	virtual		PxParticleReadDataFlags		getParticleReadDataFlags()								const	= 0;

	/**
	\brief Sets particle read data flags.
	\param flag Member of PxParticleReadDataFlag.
	\param val New flag value.
	@see PxParticleReadDataFlags
	*/
	virtual		void						setParticleReadDataFlag(PxParticleReadDataFlag::Enum flag, bool val)= 0;

protected:
	PX_INLINE								PxParticleBase(PxType concreteType, PxBaseFlags baseFlags) : PxActor(concreteType, baseFlags) {}
	PX_INLINE								PxParticleBase(PxBaseFlags baseFlags) : PxActor(baseFlags) {}
	virtual									~PxParticleBase() {}
	virtual		bool						isKindOf(const char* name) const { return !strcmp("PxParticleBase", name) || PxActor::isKindOf(name); }

//@}
/************************************************************************************************/
};

PX_DEPRECATED PX_INLINE PxParticleBase*			PxActor::isParticleBase()			{ return is<PxParticleBase>();			}
PX_DEPRECATED PX_INLINE const PxParticleBase*	PxActor::isParticleBase()	const	{ return is<PxParticleBase>();			}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

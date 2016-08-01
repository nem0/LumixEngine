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


#ifndef PX_PHYSICS_NX_CLOTH_TYPES
#define PX_PHYSICS_NX_CLOTH_TYPES
/** \addtogroup cloth
  @{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxFlags.h"

#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
   \brief flag for behaviors of the cloth solver
   \details Defines flags to turn on/off features of the cloth solver.
   The flag can be set during the cloth object construction (\see PxPhysics.createCloth() ),
   or individually after the cloth has been created (\see PxCloth.setClothFlag() ).
 */
struct PxClothFlag
{
	enum Enum
	{
		eGPU			 = (1<<0), //!< turn on/off gpu based solver
		eSWEPT_CONTACT	 = (1<<1), //!< use swept contact (continuous collision)
		eSCENE_COLLISION = (1<<2), //!< collide against rigid body shapes in scene
		eCOUNT			 = 3	   // internal use only
	};
};

typedef PxFlags<PxClothFlag::Enum,PxU16> PxClothFlags;
PX_FLAGS_OPERATORS(PxClothFlag::Enum, PxU16)

/**
   \brief Per particle data for cloth.
   \details Defines position of the cloth particle as well as inverse mass.
   When inverse mass is set to 0, the particle gets fully constrained
   to the position during simulation.
   \see PxPhysics.createCloth()
   \see PxCloth.setParticles()
*/
struct PxClothParticle
{
	PxVec3 pos;			//!< position of the particle (in cloth local space)
	PxReal invWeight;	//!< inverse mass of the particle. If set to 0, the particle is fully constrained.

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothParticle() {}
	PxClothParticle(const PxVec3& pos_, PxReal invWeight_) 
		: pos(pos_), invWeight(invWeight_){}
};

/**
\brief Constraints for cloth particle motion.
\details Defines a spherical volume to which the motion of a particle should be constrained.
@see PxCloth.setMotionConstraints()
*/
struct PxClothParticleMotionConstraint
{
	PxVec3 pos;			//!< Center of the motion constraint sphere (in cloth local space)
	PxReal radius;		//!< Maximum distance the particle can move away from the sphere center.

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothParticleMotionConstraint() {}
	PxClothParticleMotionConstraint(const PxVec3& p, PxReal r) 
		: pos(p), radius(r){}
};

/**
\brief Separation constraints for cloth particle movement
\details Defines a spherical volume such that corresponding particles should stay outside.
@see PxCloth.setSeparationConstraints()
*/
struct PxClothParticleSeparationConstraint
{
	PxVec3 pos;			//!< Center of the constraint sphere (in cloth local space)
	PxReal radius;		//!< Radius of the constraint sphere such that the particle stay outside of this sphere.

	/**
	\brief Default constructor, performs no initialization.
	*/
	PxClothParticleSeparationConstraint() {}
	PxClothParticleSeparationConstraint(const PxVec3& p, PxReal r) 
		: pos(p), radius(r){}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

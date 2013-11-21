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
// Copyright (c) 2008-2012 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_NX_CLOTH_TYPES
#define PX_PHYSICS_NX_CLOTH_TYPES
/** \addtogroup cloth
  @{
*/

#include "PxPhysX.h"
#include "foundation/PxFlags.h"

#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Solver configuration parameters for a cloth fabric phase type

@see PxCloth.setPhaseSolverConfig()
@see PxClothFabric for information on actual phase data in cloth fabric
*/
struct PxClothPhaseSolverConfig
{
	enum SolverType
	{
		eINVALID, //!< solver is invalid and disabled
		eFAST, //!< Fast solver that may stretch 
		eSTIFF, //!< Stiff solver that handles stiff fabric well (but slower than eFAST)
		eBENDING, //!< Bending solver (use with eBENDING_ANGLE phases only)
		eZEROSTRETCH, //!< solver that guarantees fibers do not stretch (but not momentum preserving)
		eSHEARING, //!< (not yet implemented)
	};

	/**
	\brief The type of solver to use for a specific phase type.

	In general, eFAST solver is faster than eSTIFF, but may converge slowly <br>
	for stiff fibers. So it's the best to use eSTIFF for fibers that are <br>
	desired to be stiff (e.g. along vertical edges) and use eFAST for other fibers.

	The default is set to eFAST for all the fibers.
	*/
	SolverType solverType;

	/**
	\brief Stiffness of the cloth solver.

	Defines for the fiber edges how much of the distance error between current length and rest length to correct per iteration step (1/solverFequency).
	A value of 0 means no correction, a value of 1 corrects to rest length.
	*/
	PxReal stiffness;

	/**
	\brief Stiffness of the cloth solver under certain limits.

	\note Applies to #eFAST solver only

	@see stretchLimit
	*/
	PxReal stretchStiffness;

	/**
	\brief Limit to control when stretchStiffness has to be applied.

	StretchStiffness controls the target convergence rate in the solver if the ratio between fiber edge length and rest length lies within the following limits:
	
	1.0 < edgelength/restlength < stretchLimit

	\note Applies to #eFAST solver only
	*/
	PxReal stretchLimit;

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxClothPhaseSolverConfig();
};

PX_INLINE PxClothPhaseSolverConfig::PxClothPhaseSolverConfig()
{
	solverType = eSTIFF;
	stiffness = 1.0f;
	stretchStiffness = 1.0f;
	stretchLimit = 1.0f;
}


/**
   \brief flag for behaviors of the cloth solver

   Defines flags to turn on/off for each feature of the cloth solver.

   The flag can be set during the cloth object construction (\see PxPhysics.createCloth() ),<br>
   or individually after the cloth has been created (\see PxCloth.setClothFlag() )
 */
struct PxClothFlag
{
	enum Enum
	{
		eSWEPT_CONTACT	= (1<<0), //!< use swept contact (continuous collision)
		eGPU			= (1<<1) //! turn on/off gpu based solver
	};
};

typedef PxFlags<PxClothFlag::Enum,PxU16> PxClothFlags;
PX_FLAGS_OPERATORS(PxClothFlag::Enum, PxU16);

/**
   \brief per particle data for cloth

   Defines position of the cloth particle as well as inverse mass.
   When inverse mass is set to 0, the particle gets fully constrained
   to the position during simulation.

   \see PxPhysics.createCloth()
   \see PxCloth.setParticles()
*/
struct PxClothParticle
{
	PxVec3 pos;			//!< position of the particle (in cloth local space)
	PxReal invWeight;	//!< inverse mass of the particle. If set to 0, the particle is fully constrained.
};

/**
\brief Constraints for cloth particle motion.

Defines a spherical volume to which the motion of a particle should be constrained.

@see PxCloth.setMotionConstraints()
*/
struct PxClothParticleMotionConstraint
{
	PxVec3 pos;			//!< Center of the motion constraint sphere (in cloth local space)
	PxReal radius;		//!< Maximum distance the particle can move away from the sphere center.
};

/**
\brief Separation constraints for cloth particle movement

Defines a spherical volume such that corresponding particles should stay outside.

@see PxCloth.setSeparationConstraints()
*/
struct PxClothParticleSeparationConstraint
{
	PxVec3 pos;			//!< Center of the constraint sphere (in cloth local space)
	PxReal radius;		//!< Radius of the constraint sphere such that the particle stay outside of this sphere.
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

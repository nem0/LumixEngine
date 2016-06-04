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


#ifndef PX_PHYSICS_NX
#define PX_PHYSICS_NX

/** Configuration include file for PhysX SDK */

/** \addtogroup physics
@{
*/

#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

// Exposing the ParticleSystem API. Run API meta data generation in Tools/PhysXMetaDataGenerator when changing.
#define PX_USE_PARTICLE_SYSTEM_API 1

// Exposing of the Cloth API. Run API meta data generation in Tools/PhysXMetaDataGenerator when changing.
#define PX_USE_CLOTH_API 1

// Exposing the Inverted Stepper feature.
#define PX_ENABLE_INVERTED_STEPPER_FEATURE 0


class PxShape;

class PxRigidStatic;
class PxRigidDynamic;
class PxConstraint;
class PxConstraintDesc;

class PxArticulation;

class PxParticleSystem;
class PxParticleFluid;

class PxClothFabric;
class PxCloth;
class PxClothParticleData;


class PxMaterial;

class PxScene;
class PxSceneDesc;
class PxTolerancesScale;

class PxVisualDebugger;

class PxAggregate;

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

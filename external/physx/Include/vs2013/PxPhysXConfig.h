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

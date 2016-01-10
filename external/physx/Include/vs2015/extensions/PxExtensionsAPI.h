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


#ifndef PX_EXTENSIONS_API_H
#define PX_EXTENSIONS_API_H
/** \addtogroup extensions
  @{
*/

#include "foundation/PxErrorCallback.h"

#include "extensions/PxDefaultAllocator.h"

#include "extensions/PxConstraintExt.h"

#include "extensions/PxDistanceJoint.h"
#include "extensions/PxFixedJoint.h"
#include "extensions/PxPrismaticJoint.h"
#include "extensions/PxRevoluteJoint.h"
#include "extensions/PxSphericalJoint.h"
#include "extensions/PxD6Joint.h"

#include "extensions/PxDefaultSimulationFilterShader.h"
#include "extensions/PxDefaultErrorCallback.h"
#include "extensions/PxDefaultStreams.h"

#include "extensions/PxRigidBodyExt.h"
#include "extensions/PxShapeExt.h"
#include "extensions/PxParticleExt.h"
#include "extensions/PxTriangleMeshExt.h"
#include "extensions/PxSerialization.h"
#include "extensions/PxVisualDebuggerExt.h"
#include "extensions/PxStringTableExt.h"

#include "extensions/PxDefaultCpuDispatcher.h"

#include "extensions/PxSmoothNormals.h"

#include "extensions/PxSimpleFactory.h"

#include "extensions/PxVisualDebuggerExt.h"

#include "extensions/PxStringTableExt.h"

#include "extensions/PxClothFabricCooker.h"

#include "extensions/PxBroadPhaseExt.h"

#include "extensions/PxClothMeshQuadifier.h"

#ifdef PX_PS3
#include "extensions/ps3/PxPS3Extension.h"
#include "extensions/ps3/PxDefaultSpuDispatcher.h"
#endif

#ifdef PX_WIIU
#include "extensions/wiiu/PxWiiUExtension.h"
#include "extensions/wiiu/PxWiiUFileHandle.h"
#endif

/** \brief Initialize the PhysXExtensions library. 

This should be called before calling any functions or methods in extensions which may require allocation. 
\note This function does not need to be called before creating a PxDefaultAllocator object.

\param physics a PxPhysics object

@see PxCloseExtensions PxFoundation PxPhysics
*/

PX_C_EXPORT bool PX_CALL_CONV PxInitExtensions(physx::PxPhysics& physics);

/** \brief Shut down the PhysXExtensions library. 

This function should be called to cleanly shut down the PhysXExtensions library before application exit. 

\note This function is required to be called to release foundation usage.

@see PxInitExtensions
*/

PX_C_EXPORT void PX_CALL_CONV PxCloseExtensions();

/** @} */
#endif // PX_EXTENSIONS_API_H

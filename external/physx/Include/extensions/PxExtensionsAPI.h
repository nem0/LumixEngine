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

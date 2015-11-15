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


#ifndef PX_PHYSICS_NXPHYSICS_API
#define PX_PHYSICS_NXPHYSICS_API
/** \addtogroup physics
@{
*/

/**
This is the main include header for the Physics SDK, for users who
want to use a single #include file.

Alternatively, one can instead directly #include a subset of the below files.
*/

// Foundation SDK 
#include "foundation/Px.h"
#include "foundation/PxAllocatorCallback.h"
#include "foundation/PxAssert.h"
#include "foundation/PxBitAndData.h"
#include "foundation/PxBounds3.h"
#include "foundation/PxBroadcastingAllocator.h"
#include "foundation/PxErrorCallback.h"
#include "foundation/PxErrors.h"
#include "foundation/PxFlags.h"
#include "foundation/PxFoundation.h"
#include "foundation/PxIntrinsics.h"
#include "foundation/PxIO.h"
#include "foundation/PxMat33.h"
#include "foundation/PxMat44.h"
#include "foundation/PxMath.h"
#include "foundation/PxPlane.h"
#include "foundation/PxPreprocessor.h"
#include "foundation/PxQuat.h"
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxStrideIterator.h"
#include "foundation/PxString.h"
#include "foundation/PxTransform.h"
#include "foundation/PxUnionCast.h"
#include "foundation/PxVec2.h"
#include "foundation/PxVec3.h"
#include "foundation/PxVec4.h"
#include "foundation/PxVersionNumber.h"

//Not physics specific utilities and common code
#include "common/PxCoreUtilityTypes.h"
#include "common/PxMathUtils.h"
#include "common/PxPhysXCommonConfig.h"
#include "common/PxRenderBuffer.h"
#include "common/PxBase.h"
#include "common/PxTolerancesScale.h"
#include "common/PxTypeInfo.h"
#include "common/PxStringTable.h"
#include "common/PxSerializer.h"
#include "common/PxMetaData.h"
#include "common/PxMetaDataFlags.h"
#include "common/PxSerialFramework.h"
#include "common/PxPhysicsInsertionCallback.h"

//Profiling 
#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileCompileTimeEventFilter.h"
#include "physxprofilesdk/PxProfileContextProvider.h"
#include "physxprofilesdk/PxProfileEventBufferClient.h"
#include "physxprofilesdk/PxProfileEventBufferClientManager.h"
#include "physxprofilesdk/PxProfileEventFilter.h"
#include "physxprofilesdk/PxProfileEventHandler.h"
#include "physxprofilesdk/PxProfileEventId.h"
#include "physxprofilesdk/PxProfileEventMutex.h"
#include "physxprofilesdk/PxProfileEventNames.h"
#include "physxprofilesdk/PxProfileEvents.h"
#include "physxprofilesdk/PxProfileEventSender.h"
#include "physxprofilesdk/PxProfileEventSystem.h"
#include "physxprofilesdk/PxProfileMemoryEventTypes.h"
#include "physxprofilesdk/PxProfileScopedEvent.h"
#include "physxprofilesdk/PxProfileZone.h"
#include "physxprofilesdk/PxProfileZoneManager.h"

//Connecting to Visual Debugger Directly
#include "physxvisualdebuggersdk/PvdConnection.h"
#include "physxvisualdebuggersdk/PvdConnectionFlags.h"
#include "physxvisualdebuggersdk/PvdConnectionManager.h"
#include "physxvisualdebuggersdk/PvdDataStream.h"
#include "physxvisualdebuggersdk/PvdErrorCodes.h"
#include "physxvisualdebuggersdk/PvdNetworkStreams.h"

//Connecting the SDK to Visual Debugger
#include "pvd/PxVisualDebugger.h"

//Task Manager
#include "pxtask/PxCpuDispatcher.h"
#include "pxtask/PxCudaContextManager.h"
#include "pxtask/PxCudaMemoryManager.h"
#include "pxtask/PxGpuCopyDesc.h"
#include "pxtask/PxGpuCopyDescQueue.h"
#include "pxtask/PxGpuDispatcher.h"
#include "pxtask/PxGpuTask.h"
#include "pxtask/PxSpuDispatcher.h"
#include "pxtask/PxSpuTask.h"
#include "pxtask/PxTask.h"
#include "pxtask/PxTaskManager.h"


//Geometry Library
#include "geometry/PxBoxGeometry.h"
#include "geometry/PxCapsuleGeometry.h"
#include "geometry/PxConvexMesh.h"
#include "geometry/PxConvexMeshGeometry.h"
#include "geometry/PxGeometry.h"
#include "geometry/PxGeometryHelpers.h"
#include "geometry/PxGeometryQuery.h"
#include "geometry/PxHeightField.h"
#include "geometry/PxHeightFieldDesc.h"
#include "geometry/PxHeightFieldFlag.h"
#include "geometry/PxHeightFieldGeometry.h"
#include "geometry/PxHeightFieldSample.h"
#include "geometry/PxMeshQuery.h"
#include "geometry/PxMeshScale.h"
#include "geometry/PxPlaneGeometry.h"
#include "geometry/PxSimpleTriangleMesh.h"
#include "geometry/PxSphereGeometry.h"
#include "geometry/PxTriangle.h"
#include "geometry/PxTriangleMesh.h"
#include "geometry/PxTriangleMeshGeometry.h"


// PhysX Core SDK
#include "PxActor.h"
#include "PxAggregate.h"
#include "PxArticulation.h"
#include "PxArticulationJoint.h"
#include "PxArticulationLink.h"
#include "PxBatchQuery.h"
#include "PxBatchQueryDesc.h"
#include "PxClient.h"
#include "PxConstraint.h"
#include "PxConstraintDesc.h"
#include "PxContact.h"
#include "PxContactModifyCallback.h"
#include "PxDeletionListener.h"
#include "PxFiltering.h"
#include "PxForceMode.h"
#include "PxLockedData.h"
#include "PxMaterial.h"
#include "PxPhysics.h"
#include "PxPhysXConfig.h"
#include "PxQueryFiltering.h"
#include "PxQueryReport.h"
#include "PxRigidActor.h"
#include "PxRigidBody.h"
#include "PxRigidDynamic.h"
#include "PxRigidStatic.h"
#include "PxScene.h"
#include "PxSceneDesc.h"
#include "PxSceneLock.h"
#include "PxShape.h"
#include "PxSimulationEventCallback.h"
#include "PxSimulationStatistics.h"
#include "PxSpatialIndex.h"
#include "PxVisualizationParameter.h"
#include "PxVolumeCache.h"

//Character Controller
#include "characterkinematic/PxBoxController.h"
#include "characterkinematic/PxCapsuleController.h"
#include "characterkinematic/PxCharacter.h"
#include "characterkinematic/PxController.h"
#include "characterkinematic/PxControllerBehavior.h"
#include "characterkinematic/PxControllerManager.h"
#include "characterkinematic/PxControllerObstacles.h"
#include "characterkinematic/PxExtended.h"

//Cloth Simulation
#if PX_USE_CLOTH_API
#include "cloth/PxCloth.h"
#include "cloth/PxClothCollisionData.h"
#include "cloth/PxClothFabric.h"
#include "cloth/PxClothParticleData.h"
#include "cloth/PxClothTypes.h"
#endif

//Cooking (data preprocessing)
#include "cooking/Pxc.h"
#include "cooking/PxConvexMeshDesc.h"
#include "cooking/PxCooking.h"
#include "cooking/PxGaussMapLimit.h"
#include "cooking/PxTriangleMeshDesc.h"

//Extensions to the SDK
#include "extensions/PxDefaultStreams.h"
#include "extensions/PxDistanceJoint.h"
#include "extensions/PxExtensionsAPI.h"
#include "extensions/PxFixedJoint.h"
#include "extensions/PxJoint.h"
#include "extensions/PxJointLimit.h"
#include "extensions/PxParticleExt.h"
#include "extensions/PxPrismaticJoint.h"
#include "extensions/PxRevoluteJoint.h"
#include "extensions/PxRigidBodyExt.h"
#include "extensions/PxShapeExt.h"
#include "extensions/PxSimpleFactory.h"
#include "extensions/PxSmoothNormals.h"
#include "extensions/PxSphericalJoint.h"
#include "extensions/PxStringTableExt.h"
#include "extensions/PxTriangleMeshExt.h"
#include "extensions/PxVisualDebuggerExt.h"
#include "extensions/PxDefaultBufferedProfiler.h"

//Serialization
#include "extensions/PxSerialization.h"
#include "extensions/PxBinaryConverter.h"
#include "extensions/PxRepXSerializer.h"
#include "extensions/PxJointRepXSerializer.h"

//Particle Simulation
#if PX_USE_PARTICLE_SYSTEM_API
#include "particles/PxParticleBase.h"
#include "particles/PxParticleBaseFlag.h"
#include "particles/PxParticleCreationData.h"
#include "particles/PxParticleFlag.h"
#include "particles/PxParticleFluid.h"
#include "particles/PxParticleFluidReadData.h"
#include "particles/PxParticleReadData.h"
#include "particles/PxParticleSystem.h"
#endif

//Vehicle Simulation
#include "vehicle/PxVehicleComponents.h"
#include "vehicle/PxVehicleDrive.h"
#include "vehicle/PxVehicleDrive4W.h"
#include "vehicle/PxVehicleDriveTank.h"
#include "vehicle/PxVehicleSDK.h"
#include "vehicle/PxVehicleShaders.h"
#include "vehicle/PxVehicleTireFriction.h"
#include "vehicle/PxVehicleUpdate.h"
#include "vehicle/PxVehicleUtilControl.h"
#include "vehicle/PxVehicleUtilSetup.h"
#include "vehicle/PxVehicleUtilTelemetry.h"
#include "vehicle/PxVehicleWheels.h"
#include "vehicle/PxVehicleNoDrive.h"
#include "vehicle/PxVehicleDriveNW.h"

/** @} */
#endif

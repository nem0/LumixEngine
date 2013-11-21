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


#ifndef PX_PARTICLE_GPU_H
#define PX_PARTICLE_GPU_H
/** \addtogroup particles
  @{
*/

#include "common/PxPhysXCommon.h"

#if PX_SUPPORT_GPU_PHYSX

#ifndef PX_DOXYGEN
namespace physx
{
#endif

namespace pxtask
{
	class CudaContextManager;
}	

/**
\brief API for gpu specific particle functionality.
*/
class PxParticleGpu
{
public:

	/**
	\brief Mirrors a triangle mesh onto the GPU memory corresponding to the given CUDA context manager, used for collision with GPU accelerated particle systems.

	Meshes are automatically mirrored on contact with particles. With this method, the mirroring can be explicitly triggered.

	\param[in] triangleMesh to be mirrored.
	\param[in] contextManager corresponding to target GPU memory space.
	\return false if mesh isn't mirrored, true otherwise.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static bool createTriangleMeshMirror(const class PxTriangleMesh& triangleMesh, pxtask::CudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored triangle mesh from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the mesh.

	\param[in] triangleMesh for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseTriangleMeshMirror(const class PxTriangleMesh& triangleMesh, pxtask::CudaContextManager* contextManager = NULL);
	
	/**
	\brief Mirrors a height field onto the GPU memory corresponding to the given CUDA context manager, used for collision with GPU accelerated particle systems.

	Height fields are automatically mirrored on contact with particles. With this method, the mirroring can be explicitly triggered.

	\param[in] heightField to be mirrored.
	\param[in] contextManager corresponding to target GPU memory space.
	\return false if height field isn't mirrored, true otherwise.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static bool createHeightFieldMirror(const class PxHeightField& heightField, pxtask::CudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored height field from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the height field.

	\param[in] heightField for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseHeightFieldMirror(const class PxHeightField& heightField, pxtask::CudaContextManager* contextManager = NULL);
	
	/**
	\brief Mirrors a convex mesh onto the GPU memory corresponding to the given CUDA context manager, used for collision with GPU accelerated particle systems.

	Meshes are automatically mirrored on contact with particles. With this method, the mirroring can be explicitly triggered.

	\param[in] convexMesh to be mirrored.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.
	\return false if mesh isn't mirrored, true otherwise.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static bool createConvexMeshMirror(const class PxConvexMesh& convexMesh, pxtask::CudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored height field from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the convex mesh.

	\param[in] convexMesh for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseConvexMeshMirror(const class PxConvexMesh& convexMesh, pxtask::CudaContextManager* contextManager = NULL);

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif // PX_SUPPORT_GPU_PHYSX

/** @} */
#endif //PX_PARTICLE_GPU_H

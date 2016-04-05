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


#ifndef PX_PARTICLE_GPU_H
#define PX_PARTICLE_GPU_H
/** \addtogroup particles
  @{
*/

#include "common/PxPhysXCommonConfig.h"

#if PX_SUPPORT_GPU_PHYSX

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxCudaContextManager;

/**
\brief Triangle mesh cache statistics.
@see PxParticleGpu.getTriangleMeshCacheStatistics
*/
struct PxTriangleMeshCacheStatistics
{
	PxU32	bytesTotal;			//!< Size of  the tri mesh cache in bytes
	PxU32	bytesUsed;			//!< Number of bytes used for caching of current frame
	PxU32	bytesUsedMax;		//!< Maximum amount of cache memory used since "setTriangleMeshCacheSizeHint" was called
	PxReal	percentageHitrate;	//!< Hit rates % of current frame

	PxTriangleMeshCacheStatistics() : bytesTotal(0), bytesUsed(0), bytesUsedMax(0), percentageHitrate(0.0f) {}
};

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
	PX_PHYSX_CORE_API static bool createTriangleMeshMirror(const class PxTriangleMesh& triangleMesh, PxCudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored triangle mesh from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the mesh.

	\param[in] triangleMesh for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseTriangleMeshMirror(const class PxTriangleMesh& triangleMesh, PxCudaContextManager* contextManager = NULL);
	
	/**
	\brief Mirrors a height field onto the GPU memory corresponding to the given CUDA context manager, used for collision with GPU accelerated particle systems.

	Height fields are automatically mirrored on contact with particles. With this method, the mirroring can be explicitly triggered.

	\param[in] heightField to be mirrored.
	\param[in] contextManager corresponding to target GPU memory space.
	\return false if height field isn't mirrored, true otherwise.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static bool createHeightFieldMirror(const class PxHeightField& heightField, PxCudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored height field from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the height field.

	\param[in] heightField for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseHeightFieldMirror(const class PxHeightField& heightField, PxCudaContextManager* contextManager = NULL);
	
	/**
	\brief Mirrors a convex mesh onto the GPU memory corresponding to the given CUDA context manager, used for collision with GPU accelerated particle systems.

	Meshes are automatically mirrored on contact with particles. With this method, the mirroring can be explicitly triggered.

	\param[in] convexMesh to be mirrored.
	\param[in] contextManager corresponding to target GPU memory space. Setting this to NULL implies removing the mirror from all contexts it was mirrored to.
	\return false if mesh isn't mirrored, true otherwise.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static bool createConvexMeshMirror(const class PxConvexMesh& convexMesh, PxCudaContextManager& contextManager);
	
	/**
	\brief Removes a mirrored height field from the GPU memory corresponding to the given CUDA context manager.

	Removing the mirror fails if any particle system is still in contact with the convex mesh.

	\param[in] convexMesh for which mirror should be removed.
	\param[in] contextManager corresponding to target GPU memory space.

	@see PxParticleBaseFlag.eGPU
	*/
	PX_PHYSX_CORE_API static void releaseConvexMeshMirror(const class PxConvexMesh& convexMesh, PxCudaContextManager* contextManager = NULL);

	/**
	\brief Explicitly flush the cuda push buffer when the number of kernel launches is equal to cudaFlushCount.

	By explicitly flushing the cuda push buffer, the kernel launch latencies can potentially be reduced.
	By default, cuda kernels are queued into a command buffer and flushed after a time elapsed determined by the cuda driver.  This is known as the launch latencies.
	Depending on the scene, the launch latencies can potentially be reduced by forcing the cuda driver to flush early when the command buffer is partially filled rather than wait until the command buffer is full.
	However, not all scenes benefit from flushing because a flush command incurs overheads.
	It is important to measure the launch latencies using a profiling tool such as NSight to determine the optimum cudaFlushCount to set.

	\param[in] scene which explicit flushing is applied.
	\param[in] cudaFlushCount is the count down counter for explicit flushing.
	*/
	PX_PHYSX_CORE_API static void setExplicitCudaFlushCountHint(const class PxScene& scene, PxU32 cudaFlushCount);

	/**
	\brief Sets the triangle mesh cache size for particles collision. This feature is not supported on Tesla and Fermi.

	Sets the amount of memory for triangle mesh cache. The optimal size depends on the scene (i.e. triangle mesh density and particle distribution).
	One can fine tune the size by querying the triangle mesh cache statistics to determine the cache utilization and hit rates.

	\param[in] scene which triangle mesh cache is set.
	\param[in] size is the amount of memory (in bytes).
	\return false if cache memory isn't allocated, true otherwise.
	*/
	PX_PHYSX_CORE_API static bool setTriangleMeshCacheSizeHint(const class PxScene& scene, PxU32 size);

	/**
	\brief Gets the triangle mesh cache statistics. This feature is not supported on Tesla and Fermi.

	Gets the usage statistics for the triangle mesh cache.

	\param[in] scene for which the triangle mesh cache is set.
	\return stats of triangle mesh cache.  The values are zeroed if triangle mesh cache is not supported.

	@see PxTriangleMeshCacheStatistics 
	*/
	PX_PHYSX_CORE_API static PxTriangleMeshCacheStatistics getTriangleMeshCacheStatistics(const class PxScene& scene);

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif // PX_SUPPORT_GPU_PHYSX

/** @} */
#endif //PX_PARTICLE_GPU_H

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


#ifndef PX_PHYSICS_EXTENSIONS_DEFAULT_CPU_DISPATCHER_H
#define PX_PHYSICS_EXTENSIONS_DEFAULT_CPU_DISPATCHER_H
/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "pxtask/PxCpuDispatcher.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A default implementation for a CPU task dispatcher.

@see PxDefaultCpuDispatcherCreate() PxCpuDispatcher
*/
class PxDefaultCpuDispatcher: public physx::PxCpuDispatcher
{
public:
	/**
	\brief Deletes the dispatcher.
	
	Do not keep a reference to the deleted instance.

	@see PxDefaultCpuDispatcherCreate()
	*/
	virtual void release() = 0;

	/**
	\brief Enables profiling at task level.

	\note By default enabled only in profiling builds.
	
	\param[in] runProfiled True if tasks should be profiled.
	*/
	virtual void setRunProfiled(bool runProfiled) = 0;

	/**
	\brief Checks if profiling is enabled at task level.

	\return True if tasks should be profiled.
	*/
	virtual bool getRunProfiled() const = 0;
};


/**
\brief Create default dispatcher, extensions SDK needs to be initialized first.

\param[in] numThreads Number of worker threads the dispatcher should use.
\param[in] affinityMasks Array with affinity mask for each thread. If not defined, default masks will be used.

\note numThreads may be zero in which case no worker thread are initialized and
simulation tasks will be executed on the thread that calls PxScene::simulate()

@see PxDefaultCpuDispatcher
*/
PxDefaultCpuDispatcher* PxDefaultCpuDispatcherCreate(PxU32 numThreads, PxU32* affinityMasks = NULL);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

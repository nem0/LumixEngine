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


#ifndef PX_PHYSICS_EXTENSIONS_DEFAULT_CPU_DISPATCHER_H
#define PX_PHYSICS_EXTENSIONS_DEFAULT_CPU_DISPATCHER_H
/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommon.h"
#include "pxtask/PxCpuDispatcher.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A default implementation for a CPU task dispatcher.

@see physx::pxtask::CpuDispatcher
*/

class PxDefaultCpuDispatcher: public physx::pxtask::CpuDispatcher
{
public:
	/**
	\brief Deletes the dispatcher.
	
	Do not keep a reference to the deleted instance.

	@see PxDefaultCpuDispatcherCreate()
	*/
	virtual void release() = 0;
};


/**
\brief Create default dispatcher, extensions SDK needs to be initialized first.

\param[in] numThreads Number of worker threads the dispatcher should use.
\param[in] affinityMasks Array with affinity mask for each thread. If not defined, default masks will be used.

@see PxDefaultCpuDispatcher
*/
PxDefaultCpuDispatcher* PxDefaultCpuDispatcherCreate(PxU32 numThreads, PxU32* affinityMasks = NULL);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

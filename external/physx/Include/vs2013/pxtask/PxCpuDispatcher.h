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

#ifndef PX_CPU_DISPATCHER_H
#define PX_CPU_DISPATCHER_H

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxBaseTask;

/** 
 \brief A CpuDispatcher is responsible for scheduling the execution of tasks passed to it by the SDK.

 A typical implementation would for example use a thread pool with the dispatcher
 pushing tasks onto worker thread queues or a global queue.

 @see PxBaseTask
 @see PxTask
 @see PxTaskManager
*/
class PxCpuDispatcher
{
public:
	/**
	\brief Called by the TaskManager when a task is to be queued for execution.
	
	Upon receiving a task, the dispatcher should schedule the task
	to run when resource is available.  After the task has been run,
	it should call the release() method and discard it's pointer.

	\param[in] task The task to be run.

	@see PxBaseTask
	*/
    virtual void submitTask( PxBaseTask& task ) = 0;

	/**
	\brief Returns the number of available worker threads for this dispatcher.
	
	The SDK will use this count to control how many tasks are submitted. By
	matching the number of tasks with the number of execution units task
	overhead can be reduced.
	*/
	virtual PxU32 getWorkerCount() const = 0;

	virtual ~PxCpuDispatcher() {}
};

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

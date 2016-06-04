/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_SPU_DISPATCHER_H
#define PX_SPU_DISPATCHER_H

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxSpuTask;

/** 
 \brief A PxSpuDispatcher 
 
 A PxSpuDispatcher is responsible for scheduling the execution of SPU tasks passed to it by the SDK.
 
 @see PxSpuTask
 @see PxTaskManager
*/
class PxSpuDispatcher
{
public:
	/**
	\brief Called by the TaskManager when an SPU task is to be queued for execution.
	
	Upon receiving a task, the dispatcher should schedule the task
	to run on any available SPUs.  After the task has been run,
	it should call the release() method and discard it's pointer.

	\param[in] task The task to be run.

	@see PxSpuTask
	*/
	virtual void submitTask( PxSpuTask& task ) = 0;

	virtual ~PxSpuDispatcher() {}
};

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

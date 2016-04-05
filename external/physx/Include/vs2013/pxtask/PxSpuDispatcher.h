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

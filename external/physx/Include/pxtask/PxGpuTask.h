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


#ifndef PX_GPU_TASK_H
#define PX_GPU_TASK_H

#include "pxtask/PxTask.h"
#include "pxtask/PxGpuDispatcher.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

namespace pxtask
{

PX_PUSH_PACK_DEFAULT

/** \brief Define the 'flavor' of a GpuTask
 *
 * Each GpuTask should have a specific function; either copying data to the
 * device, running kernels on that data, or copying data from the device.
 *
 * For optimal performance, the dispatcher should run all available HtoD tasks
 * before running all Kernel tasks, and all Kernel tasks before running any DtoH
 * tasks.  This provides maximal kernel overlap and the least number of CUDA
 * flushes.
 */
struct GpuTaskHint
{
	/// \brief Enums for the type of GPU task
	enum Enum
	{
		HostToDevice,
		Kernel,
		DeviceToHost,

		NUM_GPU_TASK_HINTS
	};
};

/**
 * \brief Task implementation for launching CUDA work
 */
class GpuTask : public Task
{
public:
	GpuTask() : mComp(NULL) {}

	/**
	 * \brief iterative "run" function for a GpuTask
	 *
	 * The GpuDispatcher acquires the CUDA context for the duration of this
	 * function call, and it is highly recommended that the GpuTask use the
	 * provided CUstream for all kernels.
	 *
	 * kernelIndex will be 0 for the initial call and incremented before each
	 * subsequent call.  Once launchInstance() returns false, its GpuTask is
	 * considered completed and is released.
	 */
	virtual bool    launchInstance(CUstream stream, int kernelIndex) = 0;

	/**
	 * \brief Returns a hint indicating the function of this task
	 */
	virtual GpuTaskHint::Enum getTaskHint() const = 0;

	/**
	 * \brief Specify a task that will have its reference count decremented
	 * when this task is released
	 */
	void setCompletionTask(BaseTask& task)
	{
		mComp = &task;
	}

	void release()
	{
		if (mComp)
		{
			mComp->removeReference();
			mComp = NULL;
		}
		Task::release();
	}

protected:
	/// \brief A pointer to the completion task
	pxtask::BaseTask* mComp;
};

PX_POP_PACK

} // end pxtask namespace

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

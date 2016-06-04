/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef PX_GPU_TASK_H
#define PX_GPU_TASK_H

#include "pxtask/PxTask.h"
#include "pxtask/PxGpuDispatcher.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

PX_PUSH_PACK_DEFAULT

/** \brief Define the 'flavor' of a PxGpuTask
 *
 * Each PxGpuTask should have a specific function; either copying data to the
 * device, running kernels on that data, or copying data from the device.
 *
 * For optimal performance, the dispatcher should run all available HtoD tasks
 * before running all Kernel tasks, and all Kernel tasks before running any DtoH
 * tasks.  This provides maximal kernel overlap and the least number of CUDA
 * flushes.
 */
struct PxGpuTaskHint
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
 * \brief PxTask implementation for launching CUDA work
 */
class PxGpuTask : public PxTask
{
public:
	PxGpuTask() : mComp(NULL) {}

	/**
	 * \brief iterative "run" function for a PxGpuTask
	 *
	 * The GpuDispatcher acquires the CUDA context for the duration of this
	 * function call, and it is highly recommended that the PxGpuTask use the
	 * provided CUstream for all kernels.
	 *
	 * kernelIndex will be 0 for the initial call and incremented before each
	 * subsequent call.  Once launchInstance() returns false, its PxGpuTask is
	 * considered completed and is released.
	 */
	virtual bool    launchInstance(CUstream stream, int kernelIndex) = 0;

	/**
	 * \brief Returns a hint indicating the function of this task
	 */
	virtual PxGpuTaskHint::Enum getTaskHint() const = 0;

	/**
	 * \brief Specify a task that will have its reference count decremented
	 * when this task is released
	 */
	void setCompletionTask(PxBaseTask& task)
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
		PxTask::release();
	}

protected:
	/// \brief A pointer to the completion task
	PxBaseTask* mComp;
};

PX_POP_PACK

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

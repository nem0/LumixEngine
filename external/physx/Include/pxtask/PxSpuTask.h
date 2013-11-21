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

#ifndef PX_SPU_TASK_H
#define PX_SPU_TASK_H

#include "pxtask/PxTask.h"
#include "pxtask/PxSpuDispatcher.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

namespace pxtask
{

/** 
 \brief A task to be executed on one or more SPUs

 Each SpuTask can run in a data parallel fashion on up to 6 SPUs. To coordinate the
 workers, each SPU will be passed it's own set of arguments.

 When all SPU workers have completed their work, the task is considered complete and the
 SpuDispatcher will call release on the task, this in turn will call removeReference() 
 on the task's continuation.

 In this way LightCpuTasks may be launched automatically at SpuTask completion and vice versa.

 Users should not need to implement or create SpuTasks directly. The SDK creates the tasks
 internally and will submit them to the TaskManager's SpuDispatcher for execution. The
 SpuDispatcher that will be used is configured on a per-scene basis through the PxSceneDesc.
 
 @see SpuDispatcher
 @see PxSceneDesc
*/
class SpuTask : public LightCpuTask
{
public:

	static const PxU32 kMaxSpus = 6;		//!< The maximum number of SPUs
	static const PxU32 kArgsPerSpu = 2;	//!< Arguments per SPU

	/**
	\brief Construct a new SpuTask object
	\param[in] elfStart The starting address of the embedded SPU binary
	\param[in] elfSize The size in bytes of the embedded SPU binary
	\param[in] numSpus The number of SPU workers this task will run across
	\param[in] args A pointer to an array of arguments, must be at least kArgsPerSpu*numSpus big
	*/
	SpuTask(const void* elfStart, PxU32 elfSize, PxU32 numSpus=1, const PxU32* args=NULL) 
		: mElfStart(elfStart)
		, mElfSize(elfSize)
		, mNumSpusToRun(numSpus)
		, mNumSpusFinished(0) 
	{
		if (args)
		{
			memcpy(mArgs, args, mNumSpusToRun*kArgsPerSpu*sizeof(PxU32));
		}		
	}

	virtual ~SpuTask() {}

	/**
	\brief Return the number of SPUs used to run this task
	*/
	PX_INLINE PxU32 getSpuCount() const
	{
		return mNumSpusToRun; 
	}

	/**
	\brief Set the number of SPUs to be used when running this task
	*/
	PX_INLINE void setSpuCount(PxU32 numSpusToRun)  
	{ 
		PX_ASSERT(numSpusToRun);
		mNumSpusToRun = numSpusToRun; 
	}

	/**
	\brief Retrieve the per-SPU argument
	\param[in] spuIndex The SPU that we want to retrieve the argument for
	\return A pointer to the parameters for the given SPU index
	*/
	PX_INLINE const PxU32* getArgs(PxU32 spuIndex) const 
	{ 
		PX_ASSERT(spuIndex < kMaxSpus); 
		return mArgs[spuIndex]; 
	}


	/**
	\brief Set the arguments for a given SPU worker
	\param[in] spuIndex The index of the SPU worker whose arguments are to be set
	\param[in] arg1 The first argument to be passed to this worker
	\param[in] arg2 The second argument to be passed to this worker	
	*/
	PX_INLINE void setArgs(PxU32 spuIndex, PxU32 arg1, PxU32 arg2)
	{
		PX_ASSERT(spuIndex < kMaxSpus);
		PxU32* arguments = mArgs[spuIndex];
		arguments[0]=arg1;
		arguments[1]=arg2;
	}

	/**
	\brief Return the address to the start of the embedded elf binary for this task
	*/
	PX_INLINE const void* getElfStart() const
	{
		return mElfStart; 
	}

	/**
	\brief Return the size of the embedded elf binary for this task
	*/
	PX_INLINE PxU32 getElfSize() const 
	{
		return mElfSize;
	}

	/**
	\brief Called by the SpuDispatcher when a SPU worker has completed, when all
	workers have completed the task is considered finished and the continuation will
	have it's ref count decremented.
	*/
	PX_INLINE void notifySpuFinish()
	{
		++mNumSpusFinished;

		// if all SPU tasks have finished clean-up and release
		if (mNumSpusFinished == mNumSpusToRun)
		{
			mNumSpusFinished = 0;
			release();			
		}
	}

	/**
	\brief Modifies LightCpuTask's behavior by submitting to the SpuDispatcher
	*/
	virtual void removeReference()
	{
		PX_ASSERT(mTm);
		mTm->decrReference(*this);
	}

	/**
	\brief Allow the task to perform PPU side intialization before the task is
	scheduled to the SPUs.
	
	This should be called by the SpuDispatcher from whichever thread calls
	submitTask(); the task should be scheduled to SPURS immediately 
	following this function returning.
	*/
	virtual void run() {}

	/** \brief Called by the SpuDispatcher after scheduling a task to the SPUs.
	
	This virtual method allows the task to perform PPU side work while the SPU
	task is running, for example using the PPU as a producer and the SPUs as 
	a consumer.
	*/
	virtual void runAfterDispatch() {}

protected:

	const void* mElfStart;				//!< A pointer to the start of the ELF image	
	PxU32 mElfSize;						//!< The size of the ELF image
	PxU32 mNumSpusToRun;				//!< The number of SPUs to run
	PxU32 mNumSpusFinished;				//!< The number of SPUs finished
	PxU32 mArgs[kMaxSpus][kArgsPerSpu];	//!< The arguments for the SPUs

} 
// wrap this in a macro so Doxygen doesn't get confused and output it
#ifndef PX_DOXYGEN
PX_ALIGN_SUFFIX(16)
#endif
;

} // end pxtask namespace

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
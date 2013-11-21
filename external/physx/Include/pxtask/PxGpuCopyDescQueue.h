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

#ifndef PX_GPU_COPY_DESC_QUEUE_H
#define PX_GPU_COPY_DESC_QUEUE_H

/*!
\file
\brief Container for enqueing copy descriptors in pinned memory
*/

#include "foundation/PxAssert.h"
#include "pxtask/PxGpuCopyDesc.h"
#include "pxtask/PxGpuDispatcher.h"
#include "pxtask/PxCudaContextManager.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

namespace pxtask
{

PX_PUSH_PACK_DEFAULT

/// \brief Container class for queueing GpuCopyDesc instances in pinned (non-pageable) CPU memory
class GpuCopyDescQueue
{
public:
	/// \brief GpuCopyDescQueue constructor
	GpuCopyDescQueue(GpuDispatcher& d)
		: mDispatcher(d)
		, mBuffer(0)
		, mStream(0)
		, mReserved(0)
		, mOccupancy(0)
		, mFlushed(0)
	{
	}

	/// \brief GpuCopyDescQueue destructor
	~GpuCopyDescQueue()
	{
		if (mBuffer)
		{
			mDispatcher.getCudaContextManager()->getMemoryManager()->free(CudaBufferMemorySpace::T_PINNED_HOST, (size_t) mBuffer);
		}
	}

	/// \brief Reset the enqueued copy descriptor list
	///
	/// Must be called at least once before any copies are enqueued, and each time the launched
	/// copies are known to have been completed.  The recommended use case is to call this at the
	/// start of each simulation step.
	void reset(CUstream stream, PxU32 reserveSize)
	{
		if (reserveSize > mReserved)
		{
			if (mBuffer)
			{
				mDispatcher.getCudaContextManager()->getMemoryManager()->free(
				    CudaBufferMemorySpace::T_PINNED_HOST,
				    (size_t) mBuffer);
				mReserved = 0;
			}
			mBuffer = (GpuCopyDesc*) mDispatcher.getCudaContextManager()->getMemoryManager()->alloc(
			              CudaBufferMemorySpace::T_PINNED_HOST,
			              reserveSize * sizeof(GpuCopyDesc),
			              NV_ALLOC_INFO("PxGpuCopyDescQueue", GPU_UTIL));
			if (mBuffer)
			{
				mReserved = reserveSize;
			}
		}

		mOccupancy = 0;
		mFlushed = 0;
		mStream = stream;
	}

	/// \brief Enqueue the specified copy descriptor, or launch immediately if no room is available
	void enqueue(GpuCopyDesc& desc)
	{
		PX_ASSERT(desc.isValid());
		if (desc.bytes == 0)
		{
			return;
		}

		if (mOccupancy < mReserved)
		{
			mBuffer[ mOccupancy++ ] = desc;
		}
		else
		{
			mDispatcher.launchCopyKernel(&desc, 1, mStream);
		}
	}

	/// \brief Launch all copies queued since the last flush or reset
	void flushEnqueued()
	{
		if (mOccupancy > mFlushed)
		{
			mDispatcher.launchCopyKernel(mBuffer + mFlushed, mOccupancy - mFlushed, mStream);
			mFlushed = mOccupancy;
		}
	}

private:
	GpuDispatcher&	mDispatcher;
	GpuCopyDesc*	mBuffer;
	CUstream        mStream;
	PxU32			mReserved;
	PxU32			mOccupancy;
	PxU32			mFlushed;

	void operator=(const GpuCopyDescQueue&); // prevent a warning...
};

PX_POP_PACK

} // end pxtask namespace

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

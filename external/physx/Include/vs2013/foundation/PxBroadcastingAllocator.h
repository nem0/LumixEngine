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


#ifndef PX_FOUNDATION_PX_BROADCASTING_ALLOCATOR_H
#define PX_FOUNDATION_PX_BROADCASTING_ALLOCATOR_H

#include "foundation/PxAllocatorCallback.h"

/** \addtogroup foundation
  @{
*/
#ifndef PX_DOXYGEN
namespace physx
{
#endif

	/**
	\brief Abstract listener class that listens to allocation and deallocation events from the
		foundation memory system.
		
	<b>Threading:</b> All methods of this class should be thread safe as it can be called from the user thread 
	or the physics processing thread(s).
	*/
	class PxAllocationListener
	{
	protected:
		virtual ~PxAllocationListener(){}

	public:
		/**
		\brief callback when memory is allocated. 
		\param size Size of the allocation in bytes.
		\param typeName Type this data is being allocated for.
		\param filename File the allocation came from.
		\param line the allocation came from.
		\param allocatedMemory memory that will be returned from the allocation.
		*/
		virtual void onAllocation( size_t size, const char* typeName, const char* filename, int line, void* allocatedMemory ) = 0;

		/**
		\brief callback when memory is deallocated.
		\param allocatedMemory memory just before allocation.
		*/
		virtual void onDeallocation( void* allocatedMemory ) = 0;
	};

	/**
	\brief Abstract base class for an application defined memory allocator that allows an external listener
	to audit the memory allocations.

	<b>Threading:</b> Register/deregister are *not* threadsafe!!!
	You need to be sure multiple threads are using this allocator when you are adding
	new listeners.
	*/
	class PxBroadcastingAllocator : public PxAllocatorCallback
	{
	protected:
		virtual ~PxBroadcastingAllocator(){}

	public:
		/**
		\brief Register an allocation listener.  This object will be notified whenever an
		allocation happens.  
		
		<b>Threading:</b>Not threadsafe if you are allocating and deallocating in another
		thread using this allocator.
		*/
		virtual void registerAllocationListener( PxAllocationListener& inListener ) = 0;
		/**
		\brief Deregister an allocation listener.  This object will no longer receive
		notifications upon allocation.
		
		<b>Threading:</b>Not threadsafe if you are allocating and deallocating in another
		thread using this allocator.
		*/
		virtual void deregisterAllocationListener( PxAllocationListener& inListener ) = 0;
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

 /** @} */
#endif // PX_FOUNDATION_PX_BROADCASTING_ALLOCATOR_H

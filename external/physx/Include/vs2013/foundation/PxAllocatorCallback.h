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


#ifndef PX_FOUNDATION_PX_ALLOCATOR_CALLBACK_H
#define PX_FOUNDATION_PX_ALLOCATOR_CALLBACK_H

/** \addtogroup foundation
@{
*/

#include "foundation/Px.h"
#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Abstract base class for an application defined memory allocator that can be used by the Px library.

\note The SDK state should not be modified from within any allocation/free function.

<b>Threading:</b> All methods of this class should be thread safe as it can be called from the user thread 
or the physics processing thread(s).
*/

class PxAllocatorCallback
{
public:
	
	/**
	\brief destructor
	*/
	virtual ~PxAllocatorCallback() {}

	/**
	\brief Allocates size bytes of memory, which must be 16-byte aligned.

	This method should never return NULL.  If you run out of memory, then
	you should terminate the app or take some other appropriate action.

	<b>Threading:</b> This function should be thread safe as it can be called in the context of the user thread 
	and physics processing thread(s).

	\param size			Number of bytes to allocate.
	\param typeName		Name of the datatype that is being allocated
	\param filename		The source file which allocated the memory
	\param line			The source line which allocated the memory
	\return				The allocated block of memory.
	*/
	virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) = 0;

	/**
	\brief Frees memory previously allocated by allocate().

	<b>Threading:</b> This function should be thread safe as it can be called in the context of the user thread 
	and physics processing thread(s).

	\param ptr Memory to free.
	*/
	virtual void deallocate(void* ptr) = 0;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

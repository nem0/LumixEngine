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


#ifndef PX_FOUNDATION_PX_MEMORY_H
#define PX_FOUNDATION_PX_MEMORY_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxPreprocessor.h"
#include "foundation/PxIntrinsics.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif


	/**
	\brief Sets the bytes of the provided buffer to zero.

	\param dest Pointer to block of memory to set zero.
	\param count Number of bytes to set to zero.

	\return Pointer to memory block (same as input)
	*/
	PX_FORCE_INLINE void* PxMemZero(void* PX_RESTRICT dest, PxU32 count)
	{
		return physx::intrinsics::memZero(dest, count);
	}

	/**
	\brief Sets the bytes of the provided buffer to the specified value.

	\param dest Pointer to block of memory to set to the specified value.
	\param c Value to set the bytes of the block of memory to.
	\param count Number of bytes to set to the specified value.

	\return Pointer to memory block (same as input)
	*/
	PX_FORCE_INLINE void* PxMemSet(void* PX_RESTRICT dest, PxI32 c, PxU32 count)
	{
		return physx::intrinsics::memSet(dest, c, count);
	}

	/**
	\brief Copies the bytes of one memory block to another. The memory blocks must not overlap.

	\note Use #PxMemMove if memory blocks overlap.

	\param dest Pointer to block of memory to copy to.
	\param src Pointer to block of memory to copy from.
	\param count Number of bytes to copy.

	\return Pointer to destination memory block
	*/
	PX_FORCE_INLINE void* PxMemCopy(void* PX_RESTRICT dest, const void* PX_RESTRICT src, PxU32 count)
	{
		return physx::intrinsics::memCopy(dest, src, count);
	}

	/**
	\brief Copies the bytes of one memory block to another. The memory blocks can overlap.

	\note Use #PxMemCopy if memory blocks do not overlap.

	\param dest Pointer to block of memory to copy to.
	\param src Pointer to block of memory to copy from.
	\param count Number of bytes to copy.

	\return Pointer to destination memory block
	*/
	PX_FORCE_INLINE void* PxMemMove(void* PX_RESTRICT dest, const void* PX_RESTRICT src, PxU32 count)
	{
		return physx::intrinsics::memMove(dest, src, count);
	}


#ifndef PX_DOXYGEN
} // namespace physx
#endif


/** @} */
#endif // PX_FOUNDATION_PX_MEMORY_H

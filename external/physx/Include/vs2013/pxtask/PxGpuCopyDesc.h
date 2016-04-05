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

#ifndef PX_GPU_COPY_DESC_H
#define PX_GPU_COPY_DESC_H

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

PX_PUSH_PACK_DEFAULT

/**
 * \brief Input descriptor for the GpuDispatcher's built-in copy kernel
 *
 * All host memory involved in copy transactions must be page-locked.
 * If more than one descriptor is passed to the copy kernel in one launch,
 * the descriptors themselves must be in page-locked memory.
 */
struct PxGpuCopyDesc
{
	/**
	 * \brief Input descriptor for the GpuDispatcher's built-in copy kernel
	 */
	enum CopyType
	{
		HostToDevice,
		DeviceToHost,
		DeviceToDevice,
		DeviceMemset32
	};

	size_t		dest;	//!< the destination 
	size_t		source; //!< the source (32bit value when type == DeviceMemset)
	size_t		bytes;	//!< the size in bytes
	CopyType	type;	//!< the memory transaction type

	/** 
	 * \brief Copy is optimally performed as 64bit words, requires 64bit alignment.  But it can
	 * gracefully degrade to 32bit copies if necessary
	 */
	PX_INLINE bool isValid()
	{
		bool ok = true;
		ok &= ((dest & 0x3) == 0);
		ok &= ((type == DeviceMemset32) || (source & 0x3) == 0);
		ok &= ((bytes & 0x3) == 0);
		return ok;
	}
};

PX_POP_PACK

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif

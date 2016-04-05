/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

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

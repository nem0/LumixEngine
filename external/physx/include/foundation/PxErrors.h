/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_FOUNDATION_PX_ERRORS_H
#define PX_FOUNDATION_PX_ERRORS_H
/** \addtogroup foundation
@{
*/

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Error codes

These error codes are passed to #PxErrorCallback

@see PxErrorCallback
*/

struct PxErrorCode
{
	enum Enum
	{
		eNO_ERROR				= 0,

		//! \brief An informational message.
		eDEBUG_INFO				= 1,

		//! \brief a warning message for the user to help with debugging
		eDEBUG_WARNING			= 2,

		//! \brief method called with invalid parameter(s)
		eINVALID_PARAMETER		= 4,

		//! \brief method was called at a time when an operation is not possible
		eINVALID_OPERATION		= 8,

		//! \brief method failed to allocate some memory
		eOUT_OF_MEMORY			= 16,

		/** \brief The library failed for some reason.
		Possibly you have passed invalid values like NaNs, which are not checked for.
		*/
		eINTERNAL_ERROR			= 32,

		//! \brief An unrecoverable error, execution should be halted and log output flushed 
		eABORT					= 64,

		//! \brief The SDK has determined that an operation may result in poor performance. 
		ePERF_WARNING			= 128,

		//! \brief A bit mask for including all errors
		eMASK_ALL				= -1
	};
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

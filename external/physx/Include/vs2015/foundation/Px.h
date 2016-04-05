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


#ifndef PX_FOUNDATION_PX_H
#define PX_FOUNDATION_PX_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxVersionNumber.h"
#include "foundation/PxSimpleTypes.h"

/** files to always include */
#include <string.h>
#include <stdlib.h>

#if defined(PX_LINUX) || defined(PX_ANDROID) || defined(PX_PSP2) || defined (PX_WIIU) || defined(PX_PS4)
#include <stdint.h> // uintptr_t, intptr_t
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif
		class PxVec2;
		class PxVec3;
		class PxVec4;
		class PxMat33;
		class PxMat44;
		class PxQuat;
		class PxTransform;
		class PxBounds3;

		class PxAllocatorCallback;
		class PxErrorCallback;

		class PxFoundation;

		/** enum for empty constructor tag*/
	    enum PxEMPTY			{	PxEmpty		};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

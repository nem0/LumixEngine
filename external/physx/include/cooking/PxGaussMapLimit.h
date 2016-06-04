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


#ifndef PX_COOKING_GAUSS_MAP_LIMIT_H
#define PX_COOKING_GAUSS_MAP_LIMIT_H

#include "cooking/PxCooking.h"
#include "foundation/PxAssert.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	PX_FORCE_INLINE PxU32 PxGetGaussMapVertexLimitForPlatform(PxPlatform::Enum targetPlatform)
	{
		//TODO: find optimal values for these empirically!!
		switch(targetPlatform)
		{
			case PxPlatform::eXENON:
				return 128;
			case PxPlatform::ePLAYSTATION3:
				return 128;
			case PxPlatform::ePC:
				return 32;
			case PxPlatform::eARM:
				return 32;
			case PxPlatform::eWIIU:
				return 128;
			default:
				PX_ALWAYS_ASSERT_MESSAGE("Unexpected platform in PxGetGaussMapVertexLimitForPlatform!");
				return 0;
		}
	}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif

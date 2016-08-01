/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_BASE_H
#define PX_PROFILE_BASE_H

#include "foundation/PxSimpleTypes.h"
#include "foundation/PxAssert.h"
#include "foundation/PxMath.h"

namespace physx { 
	namespace shdfnd {}
	namespace profile {}
}

#define PX_PROFILE_POINTER_TO_U64( pointer ) static_cast<PxU64>(reinterpret_cast<size_t>(pointer))

#endif // PX_PROFILE_BASE_H

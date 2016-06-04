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


#ifndef PX_FOUNDATION_PX_STRING_H
#define PX_FOUNDATION_PX_STRING_H

#include "foundation/PxPreprocessor.h"

#if (defined PX_WINDOWS  || defined PX_WINMODERN)
	#include "foundation/windows/PxWindowsString.h"
#elif defined PX_X360
	#include "foundation/xbox360/PxXbox360String.h"
#elif (defined PX_LINUX || defined PX_APPLE || defined PX_ANDROID || defined PX_PS4)
	#include "foundation/unix/PxUnixString.h"
#elif defined PX_PS3
	#include "foundation/ps3/PxPS3String.h"
#elif defined PX_PSP2
	#include "foundation/psp2/PxPSP2String.h"
#elif defined PX_WIIU
	#include "foundation/wiiu/PxWiiUString.h"
#elif defined PX_XBOXONE
	#include "foundation/XboxOne/PxXboxOneString.h"
#else
	#error "Platform not supported!"
#endif


#endif

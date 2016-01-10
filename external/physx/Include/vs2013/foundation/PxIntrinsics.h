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


#ifndef PX_FOUNDATION_PX_INTRINSICS_H
#define PX_FOUNDATION_PX_INTRINSICS_H

#include "foundation/PxPreprocessor.h"


#if (defined PX_WINDOWS  || defined PX_WINMODERN)
	#include "foundation/windows/PxWindowsIntrinsics.h"
#elif defined PX_X360
	#include "foundation/xbox360/PxXbox360Intrinsics.h"
#elif (defined PX_LINUX || defined PX_ANDROID || defined PX_APPLE || defined PX_PS4)
	#include "foundation/unix/PxUnixIntrinsics.h"
#elif defined PX_PS3
	#include "foundation/ps3/PxPS3Intrinsics.h"
#elif defined PX_PSP2
	#include "foundation/psp2/PxPSP2Intrinsics.h"
#elif defined PX_WIIU
	#include "foundation/wiiu/PxWiiUIntrinsics.h"
#elif defined PX_XBOXONE
	#include "foundation/XboxOne/PxXboxOneIntrinsics.h"
#else
	#error "Platform not supported!"
#endif


#endif // PX_FOUNDATION_PX_INTRINSICS_H

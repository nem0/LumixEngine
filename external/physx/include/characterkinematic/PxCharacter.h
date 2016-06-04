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


#ifndef PX_CHARACTER_H
#define PX_CHARACTER_H
/** \addtogroup character
  @{
*/

#include "foundation/Px.h"

// define API function declaration
#if defined PX_PHYSX_STATIC_LIB || defined PX_PHYSX_CHARACTER_STATIC_LIB
	#define PX_PHYSX_CHARACTER_API
#else
	#if defined(PX_WINDOWS) || defined(PX_WINMODERN)
		#if defined PX_PHYSX_CHARACTER_EXPORTS
			#define PX_PHYSX_CHARACTER_API __declspec(dllexport)
		#else
			#define PX_PHYSX_CHARACTER_API __declspec(dllimport)
		#endif
	#elif defined(PX_UNIX)
		#define PX_PHYSX_CHARACTER_API PX_UNIX_EXPORT
    #else
		#define PX_PHYSX_CHARACTER_API
    #endif
#endif

/** @} */
#endif

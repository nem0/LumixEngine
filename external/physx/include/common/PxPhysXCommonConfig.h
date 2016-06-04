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


#ifndef PX_PHYSICS_COMMON_NX
#define PX_PHYSICS_COMMON_NX

/** \addtogroup common 
@{ */

#include "foundation/Px.h"


// define API function declaration (public API only needed because of extensions)
#if defined PX_PHYSX_STATIC_LIB || defined PX_PHYSX_CORE_STATIC_LIB
	#define PX_PHYSX_CORE_API
#else
	#if (defined(PX_WINDOWS) || defined(PX_WINMODERN))
		#if defined PX_PHYSX_CORE_EXPORTS
			#define PX_PHYSX_CORE_API __declspec(dllexport)
		#else
			#define PX_PHYSX_CORE_API __declspec(dllimport)
		#endif
	#elif defined(PX_UNIX)
		#define PX_PHYSX_CORE_API PX_UNIX_EXPORT
    #else
		#define PX_PHYSX_CORE_API
    #endif
#endif

#if (defined(PX_WINDOWS) || defined(PX_WINMODERN)) && !defined(__CUDACC__)
	#if defined PX_PHYSX_COMMON_EXPORTS
		#define PX_PHYSX_COMMON_API __declspec(dllexport)
	#else
		#define PX_PHYSX_COMMON_API __declspec(dllimport)
	#endif
#elif defined(PX_UNIX)
	#define PX_PHYSX_COMMON_API PX_UNIX_EXPORT
#else
	#define PX_PHYSX_COMMON_API
#endif

// Changing these parameters requires recompilation of the SDK

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	class PxCollection;
	class PxBase;

	class PxHeightField;
	class PxHeightFieldDesc;

	class PxTriangleMesh;
	class PxConvexMesh;

	typedef PxU32 PxTriangleID;
	typedef PxU16 PxMaterialTableIndex;

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

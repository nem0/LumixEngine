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



#define THERE_IS_NO_INCLUDE_GUARD_HERE_FOR_A_REASON

#ifndef DECLARE_BASE_PVD_TYPE
#define	DECLARE_BASE_PVD_TYPE(x)
#endif

DECLARE_BASE_PVD_TYPE( PxI8 )
DECLARE_BASE_PVD_TYPE( PxU8 )
DECLARE_BASE_PVD_TYPE( PxI16 )
DECLARE_BASE_PVD_TYPE( PxU16 )
DECLARE_BASE_PVD_TYPE( PxI32 )
DECLARE_BASE_PVD_TYPE( PxU32 )
DECLARE_BASE_PVD_TYPE( PxI64 )
DECLARE_BASE_PVD_TYPE( PxU64 )
DECLARE_BASE_PVD_TYPE( PxF32 )
DECLARE_BASE_PVD_TYPE( PxF64 )
DECLARE_BASE_PVD_TYPE( PvdBool )
DECLARE_BASE_PVD_TYPE( PvdColor )
DECLARE_BASE_PVD_TYPE( String )	//Not allowed inside the object model itself
DECLARE_BASE_PVD_TYPE( StringHandle )	//StringTable::strToHandle
DECLARE_BASE_PVD_TYPE( ObjectRef )		//Reference to another object.
DECLARE_BASE_PVD_TYPE( VoidPtr )		//void*, so varies size by platform
DECLARE_BASE_PVD_TYPE( PxVec2 )
DECLARE_BASE_PVD_TYPE( PxVec3 )
DECLARE_BASE_PVD_TYPE( PxVec4 )
DECLARE_BASE_PVD_TYPE( PxBounds3 )
DECLARE_BASE_PVD_TYPE( PxQuat )
DECLARE_BASE_PVD_TYPE( PxTransform )
DECLARE_BASE_PVD_TYPE( PxMat33 )
DECLARE_BASE_PVD_TYPE( PxMat44 )
DECLARE_BASE_PVD_TYPE( PxMat34Legacy )
DECLARE_BASE_PVD_TYPE( U32Array4 )

#undef THERE_IS_NO_INCLUDE_GUARD_HERE_FOR_A_REASON
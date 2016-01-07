/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */



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

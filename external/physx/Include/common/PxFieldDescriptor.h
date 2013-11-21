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
// Copyright (c) 2008-2012 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


//#ifdef REMOVED

#ifndef PX_PHYSICS_COMMON_NX_FIELD_DESCRIPTOR
#define PX_PHYSICS_COMMON_NX_FIELD_DESCRIPTOR

/** \addtogroup common
@{
*/

#include "common/PxPhysXCommon.h"

// PX_SERIALIZATION

#include "common/PxFields.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

//! A field descriptor
struct PxFieldDescriptor
{
	// Compulsory values
					PxField::Enum	mType;			//!< Field type (bool, byte, quaternion, etc)
					const char*		mName;			//!< Field name (appears exactly as in the source file)
					PxU32			mOffset;		//!< Offset from the start of the class (ie from "this", field is located at "this"+Offset)
					PxU32			mSize;			//!< sizeof(Type)
					PxU32			mCount;			//!< Number of items of type Type (0 for dynamic sizes)
					PxU32			mOffsetSize;	//!< Offset of dynamic size param, for dynamic arrays
					PxU32			mFlags;			//!< Field parameters
	// Generic methods
					PxU32			FieldSize()								const;
	PX_FORCE_INLINE	void*			Address(void* class_ptr)				const	{ return (void*)(size_t(class_ptr) + mOffset);			}

	PX_FORCE_INLINE	void*			GetArrayAddress(void* class_ptr)		const	{ return *(void**)Address(class_ptr);					}
	PX_FORCE_INLINE	PxU32			IsStaticArray()							const	{ return mCount;										}
	PX_FORCE_INLINE	PxU32			GetStaticArraySize()					const	{ return mCount;										}
	PX_FORCE_INLINE	PxU32			IsDynamicArray()						const	{ return mOffsetSize;									}
	PX_FORCE_INLINE	PxU32			GetDynamicArraySize(void* class_ptr)	const	{ return *(PxU32*)(size_t(class_ptr) + mOffsetSize);	}
};


//~PX_SERIALIZATION

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_CONTACT_H
#define PX_CONTACT_H

#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

#ifdef PX_VC
#pragma warning(push)
#pragma warning(disable: 4324)	// Padding was added at the end of a structure because of a __declspec(align) value.
#endif

/**
\brief A contact point as used by contact modification
*/
struct PxContactPoint
{
	/**
	\brief The normal of the contacting surfaces at the contact point.  
	*/
	PX_ALIGN(16, PxVec3	normal);

	/**
	\brief The point of contact between the shapes, in world space. 
	*/
	PX_ALIGN(16, PxVec3	point);

	/**
	\brief The separation of the shapes at the contact point.  A negative separation denotes a penetration.
	*/
	PxReal	separation;

	/**
	\brief The surface index of shape 0 at the contact point.  This is used to identify the surface material.
	*/
	PxU32   internalFaceIndex0;

	/**
	\brief The surface index of shape 1 at the contact point.  This is used to identify the surface material.
	*/
	PxU32   internalFaceIndex1;
};

#ifdef PX_VC
#pragma warning(pop)
#endif

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif

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



/*
VersionNumbers:  The combination of these
numbers uniquely identifies the API, and should
be incremented when the SDK API changes.  This may
include changes to file formats.

This header is included in the main SDK header files
so that the entire SDK and everything that builds on it
is completely rebuilt when this file changes.  Thus,
this file is not to include a frequently changing
build number.  See BuildNumber.h for that.

Each of these three values should stay below 255 because
sometimes they are stored in a byte.
*/
/** \addtogroup foundation
  @{
*/
#ifndef PX_FOUNDATION_PX_VERSION_NUMBER_H
#define PX_FOUNDATION_PX_VERSION_NUMBER_H

#define PX_PHYSICS_VERSION_MAJOR 3
#define PX_PHYSICS_VERSION_MINOR 3
#define PX_PHYSICS_VERSION_BUGFIX 2

/**
The constant PX_PHYSICS_VERSION is used when creating certain PhysX module objects.
This is to ensure that the application is using the same header version as the library was built with.
*/
#define PX_PHYSICS_VERSION ((PX_PHYSICS_VERSION_MAJOR<<24) + (PX_PHYSICS_VERSION_MINOR<<16) + (PX_PHYSICS_VERSION_BUGFIX<<8) + 0)


#endif // PX_FOUNDATION_PX_VERSION_NUMBER_H

 /** @} */

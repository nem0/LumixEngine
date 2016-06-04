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

//
// Important: if you adjust the versions below, don't forget to adjust the compatibility list in
// sBinaryCompatibleVersions as well.
//

#define PX_PHYSICS_VERSION_MAJOR 3
#define PX_PHYSICS_VERSION_MINOR 3
#define PX_PHYSICS_VERSION_BUGFIX 4

/**
The constant PX_PHYSICS_VERSION is used when creating certain PhysX module objects.
This is to ensure that the application is using the same header version as the library was built with.
*/
#define PX_PHYSICS_VERSION ((PX_PHYSICS_VERSION_MAJOR<<24) + (PX_PHYSICS_VERSION_MINOR<<16) + (PX_PHYSICS_VERSION_BUGFIX<<8) + 0)


#endif // PX_FOUNDATION_PX_VERSION_NUMBER_H

 /** @} */

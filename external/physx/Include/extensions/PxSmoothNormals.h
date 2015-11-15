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


#ifndef PX_PHYSICS_EXTENSIONS_SMOOTH_NORMALS_H
#define PX_PHYSICS_EXTENSIONS_SMOOTH_NORMALS_H
/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommonConfig.h"

/**
\brief Builds smooth vertex normals over a mesh.

- "smooth" because smoothing groups are not supported here
- takes angles into account for correct cube normals computation

To use 32bit indices pass a pointer in dFaces and set wFaces to zero. Alternatively pass a pointer to 
wFaces and set dFaces to zero.

\param[in] nbTris Number of triangles
\param[in] nbVerts Number of vertices
\param[in] verts Array of vertices
\param[in] dFaces Array of dword triangle indices, or null
\param[in] wFaces Array of word triangle indices, or null
\param[out] normals Array of computed normals (assumes nbVerts vectors)
\param[in] flip Flips the normals or not
\return True on success.
*/
PX_C_EXPORT bool PX_CALL_CONV PxBuildSmoothNormals(physx::PxU32 nbTris, physx::PxU32 nbVerts, const physx::PxVec3* verts,
												   const physx::PxU32* dFaces, const physx::PxU16* wFaces, physx::PxVec3* normals, bool flip);

/** @} */
#endif

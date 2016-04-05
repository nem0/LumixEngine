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


#ifndef PX_MATHUTILS_H
#define PX_MATHUTILS_H

/** \addtogroup common
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxTransform.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif


/**
\brief finds the shortest rotation between two vectors.

\param[in] from the vector to start from
\param[in] target the vector to rotate to
\return a rotation about an axis normal to the two vectors which takes one to the other via the shortest path
*/

PX_FOUNDATION_API PxQuat PxShortestRotation(const PxVec3& from, const PxVec3& target);


/* \brief diagonalizes a 3x3 matrix y

The returned matrix satisfies M = R * D * R', where R is the rotation matrix for the output quaternion, R' its transpose, and D the diagonal matrix 

\param[in] m the matrix to diagonalize
\param[out] axes a quaternion rotation which diagonalizes the matrix 
\return the vector diagonal of the diagonalized matrix.
*/

PX_FOUNDATION_API PxVec3 PxDiagonalize(const PxMat33& m, PxQuat &axes);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

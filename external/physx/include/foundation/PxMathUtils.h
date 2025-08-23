//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2008-2021 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#ifndef PXFOUNDATION_PXMATHUTILS_H
#define PXFOUNDATION_PXMATHUTILS_H

/** \addtogroup common
  @{
*/

#include "foundation/Px.h"
#include "foundation/PxFoundationConfig.h"
#include "foundation/PxPlane.h"
#include "foundation/PxTransform.h"

#if !PX_DOXYGEN
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

/* \brief diagonalizes a 3x3 symmetric matrix y

The returned matrix satisfies M = R * D * R', where R is the rotation matrix for the output quaternion, R' its
transpose, and D the diagonal matrix

If the matrix is not symmetric, the result is undefined.

\param[in] m the matrix to diagonalize
\param[out] axes a quaternion rotation which diagonalizes the matrix
\return the vector diagonal of the diagonalized matrix.
*/
PX_FOUNDATION_API PxVec3 PxDiagonalize(const PxMat33& m, PxQuat& axes);

/** \brief creates a transform from the endpoints of a segment, suitable for an actor transform for a PxCapsuleGeometry

\param[in] p0 one end of major axis of the capsule
\param[in] p1 the other end of the axis of the capsule
\param[out] halfHeight the halfHeight of the capsule. This parameter is optional.
\return A PxTransform which will transform the vector (1,0,0) to the capsule axis shrunk by the halfHeight
*/
PX_FOUNDATION_API PxTransform PxTransformFromSegment(const PxVec3& p0, const PxVec3& p1, PxReal* halfHeight = NULL);

/** \brief creates a transform from a plane equation, suitable for an actor transform for a PxPlaneGeometry

\param[in] plane the desired plane equation
\return a PxTransform which will transform the plane PxPlane(1,0,0,0) to the specified plane
*/
PX_FOUNDATION_API PxTransform PxTransformFromPlaneEquation(const PxPlane& plane);

/** \brief creates a plane equation from a transform, such as the actor transform for a PxPlaneGeometry

\param[in] transform the transform
\return the plane
*/
PX_INLINE PxPlane PxPlaneEquationFromTransform(const PxTransform& transform)
{
	return transform.transform(PxPlane(1.f,0.f,0.f,0.f));
}


#if !PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

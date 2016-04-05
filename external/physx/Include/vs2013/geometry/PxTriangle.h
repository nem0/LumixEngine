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


#ifndef PX_PHYSICS_GEOMUTILS_PX_TRIANGLE
#define PX_PHYSICS_GEOMUTILS_PX_TRIANGLE
/** \addtogroup geomutils
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Triangle class.
*/
class PxTriangle
{
	public:
	/**
	\brief Constructor
	*/
	PX_FORCE_INLINE			PxTriangle() {}

	/**
	\brief Constructor

	\param[in] p0 Point 0
	\param[in] p1 Point 1
	\param[in] p2 Point 2
	*/
	PX_FORCE_INLINE			PxTriangle(const PxVec3& p0, const PxVec3& p1, const PxVec3& p2)
	{
		verts[0] = p0;
		verts[1] = p1;
		verts[2] = p2;
	}

	/**
	\brief Copy constructor

	\param[in] triangle Tri to copy
	*/
	PX_FORCE_INLINE			PxTriangle(const PxTriangle& triangle)
	{
		verts[0] = triangle.verts[0];
		verts[1] = triangle.verts[1];
		verts[2] = triangle.verts[2];
	}

	/**
	\brief Destructor
	*/
	PX_FORCE_INLINE			~PxTriangle() {}

	/**
	\brief Array of Vertices.
	*/
	PxVec3		verts[3];

	/**
	\brief Compute the normal of the Triangle.

	\param[out] _normal Triangle normal.
	*/
	PX_FORCE_INLINE	void	normal(PxVec3& _normal) const
	{
		_normal = (verts[1]-verts[0]).cross(verts[2]-verts[0]);
		_normal.normalize();
	}

	/**
	\brief Compute the unnormalized normal of the triangle.

	\param[out] _normal Triangle normal (not normalized).
	*/
#ifdef __SPU__
	void	denormalizedNormal(PxVec3& _normal) const
#else
	PX_FORCE_INLINE	void	denormalizedNormal(PxVec3& _normal) const
#endif
	{
		_normal = (verts[1]-verts[0]).cross(verts[2]-verts[0]);
	}

	/**
	\brief Compute the area of the triangle.

	\return Area of the triangle.
	*/
	PX_FORCE_INLINE	PxReal	area() const
	{
		const PxVec3& p0 = verts[0];
		const PxVec3& p1 = verts[1];
		const PxVec3& p2 = verts[2];
		return ((p0 - p1).cross(p0 - p2)).magnitude() * 0.5f;
	}

};


#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif

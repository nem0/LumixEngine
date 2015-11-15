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


#ifndef PX_PHYSICS_NX_CLOTHMESHDESC
#define PX_PHYSICS_NX_CLOTHMESHDESC
/** \addtogroup cooking
@{
*/

#include "common/PxPhysXCommonConfig.h"
#include "geometry/PxSimpleTriangleMesh.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Descriptor class for a cloth mesh.

@see PxCooking.cookClothMesh()

*/
class PxClothMeshDesc
{
public:

	/**
	\brief Pointer to first vertex point.
	*/
	PxBoundedData points;

	/**
	\brief Determines whether particle is simulated or static.
	A positive value denotes that the particle is being simulated, zero denotes a static particle.
	This data is used to generate tether and zero stretch constraints.
	If invMasses.data is null, all particles are assumed to be simulated 
	and no tether and zero stretch constraints are being generated.
	*/
	PxBoundedData invMasses;

	/**
	\brief Pointer to the first triangle.

	These are triplets of 0 based indices:
	vert0 vert1 vert2
	vert0 vert1 vert2
	vert0 vert1 vert2
	...

	where vert* is either a 32 or 16 bit unsigned integer. There are a total of 3*count indices.
	The stride determines the byte offset to the next index triple.
	
	This is declared as a void pointer because it is actually either an PxU16 or a PxU32 pointer.
	*/
	PxBoundedData triangles;

	/**
	\brief Pointer to the first quad.

	These are quadruples of 0 based indices:
	vert0 vert1 vert2 vert3
	vert0 vert1 vert2 vert3
	vert0 vert1 vert2 vert3
	...

	where vert* is either a 32 or 16 bit unsigned integer. There are a total of 4*count indices.
	The stride determines the byte offset to the next index quadruple.

	This is declared as a void pointer because it is actually either an PxU16 or a PxU32 pointer.
	*/
	PxBoundedData quads;

	/**
	\brief Flags bits, combined from values of the enum ::PxMeshFlag
	*/
	PxMeshFlags flags;

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxClothMeshDesc();
	/**
	\brief (re)sets the structure to the default.	
	*/
	PX_INLINE void setToDefault();
	/**
	\brief Returns true if the descriptor is valid.
	\return True if the current settings are valid
	*/
	PX_INLINE bool isValid() const;
};

PX_INLINE PxClothMeshDesc::PxClothMeshDesc()	//constructor sets to default
{
}

PX_INLINE void PxClothMeshDesc::setToDefault()
{
	*this = PxClothMeshDesc();
}

PX_INLINE bool PxClothMeshDesc::isValid() const
{
	if(points.count < 3) 	//at least 1 trig's worth of points
		return false;
	if(points.count > 0xffff && flags & PxMeshFlag::e16_BIT_INDICES)
		return false;
	if(!points.data)
		return false;
	if(points.stride < sizeof(PxVec3))	//should be at least one point's worth of data
		return false;

	if(invMasses.data && invMasses.stride < sizeof(float))
		return false;
	if(invMasses.data && invMasses.count != points.count)
		return false;

	if (!triangles.count && !quads.count)	// no support for non-indexed mesh
		return false;
	if (triangles.count && !triangles.data)
		return false;
	if (quads.count && !quads.data)
		return false;

	PxU32 indexSize = (flags & PxMeshFlag::e16_BIT_INDICES) ? sizeof(PxU16) : sizeof(PxU32);
	if(triangles.count && triangles.stride < indexSize*3) 
		return false; 
	if(quads.count && quads.stride < indexSize*4)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

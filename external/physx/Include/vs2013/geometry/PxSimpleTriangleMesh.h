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


#ifndef PX_PHYSICS_GEOMUTILS_NX_SIMPLETRIANGLEMESH
#define PX_PHYSICS_GEOMUTILS_NX_SIMPLETRIANGLEMESH
/** \addtogroup geomutils
@{
*/

#include "foundation/PxVec3.h"
#include "foundation/PxFlags.h"
#include "common/PxCoreUtilityTypes.h"
#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Enum with flag values to be used in PxSimpleTriangleMesh::flags.
*/
struct PxMeshFlag
{
	enum Enum
	{
		/**
		\brief Specifies if the SDK should flip normals.

		The PhysX libraries assume that the face normal of a triangle with vertices [a,b,c] can be computed as:
		edge1 = b-a
		edge2 = c-a
		face_normal = edge1 x edge2.

		Note: This is the same as a counterclockwise winding in a right handed coordinate system or
		alternatively a clockwise winding order in a left handed coordinate system.

		If this does not match the winding order for your triangles, raise the below flag.
		*/
		eFLIPNORMALS		=	(1<<0),
		e16_BIT_INDICES		=	(1<<1)	//<! Denotes the use of 16-bit vertex indices
	};
};

/**
\brief collection of set bits defined in PxMeshFlag.

@see PxMeshFlag
*/
typedef PxFlags<PxMeshFlag::Enum,PxU16> PxMeshFlags;
PX_FLAGS_OPERATORS(PxMeshFlag::Enum,PxU16)


/**
\brief A structure describing a triangle mesh.
*/
class PxSimpleTriangleMesh
{
public:

	/**
	\brief Pointer to first vertex point.
	*/
	PxBoundedData points;

	/**
	\brief Pointer to first triangle.

	Caller may add triangleStrideBytes bytes to the pointer to access the next triangle.

	These are triplets of 0 based indices:
	vert0 vert1 vert2
	vert0 vert1 vert2
	vert0 vert1 vert2
	...

	where vertex is either a 32 or 16 bit unsigned integer. There are numTriangles*3 indices.

	This is declared as a void pointer because it is actually either an PxU16 or a PxU32 pointer.
	*/
	PxBoundedData triangles;

	/**
	\brief Flags bits, combined from values of the enum ::PxMeshFlag
	*/
	PxMeshFlags flags;

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxSimpleTriangleMesh();	
	/**
	\brief (re)sets the structure to the default.	
	*/
	PX_INLINE void setToDefault();
	/**
	\brief returns true if the current settings are valid
	*/
	PX_INLINE bool isValid() const;
};


PX_INLINE PxSimpleTriangleMesh::PxSimpleTriangleMesh()
{
}

PX_INLINE void PxSimpleTriangleMesh::setToDefault()
{
	*this = PxSimpleTriangleMesh();
}

PX_INLINE bool PxSimpleTriangleMesh::isValid() const
{
	// Check geometry
	if(points.count > 0xffff && flags & PxMeshFlag::e16_BIT_INDICES)
		return false;
	if(!points.data)
		return false;
	if(points.stride < sizeof(PxVec3))	//should be at least one point's worth of data
		return false;

	// Check topology
	// The triangles pointer is not mandatory
	if(triangles.data)
	{
		// Indexed mesh
        PxU32 limit = (flags & PxMeshFlag::e16_BIT_INDICES) ? sizeof(PxU16)*3 : sizeof(PxU32)*3;
        if(triangles.stride < limit) 
            return false; 
	}
	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

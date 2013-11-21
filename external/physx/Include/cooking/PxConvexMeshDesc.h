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


#ifndef PX_COLLISION_NXCONVEXMESHDESC
#define PX_COLLISION_NXCONVEXMESHDESC
/** \addtogroup cooking
@{
*/

#include "foundation/PxVec3.h"
#include "foundation/PxFlags.h"
#include "common/PxCoreUtilityTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Flags which describe the format and behavior of a convex mesh.
*/
struct PxConvexFlag
{
	enum Enum
	{
		/**
		\brief Used to flip the normals if the winding order is reversed.

		The Nx libraries assume that the face normal of a triangle with vertices [a,b,c] can be computed as:
		edge1 = b-a
		edge2 = c-a
		face_normal = edge1 x edge2.

		Note: this is the same as counterclockwise winding in a right handed graphics coordinate system.

		If this does not match the winding order for your triangles, raise the below flag.
		*/
		eFLIPNORMALS		=	(1<<0),

		/**
		Denotes the use of 16-bit vertex indices in PxConvexMeshDesc::triangles.
		(otherwise, 32-bit indices are assumed)
		@see #PxConvexMeshDesc.triangles
		*/
		e16_BIT_INDICES		=	(1<<1),

		/**
		Automatically recomputes the hull from the vertices. If this flag is not set, you must provide the entire geometry manually.
		*/
		eCOMPUTE_CONVEX		=	(1<<2),	

		/**
		\brief Inflates the convex object according to skin width

		\note This flag is only used in combination with eCOMPUTE_CONVEX.

		@see PxCookingParams
		*/
		eINFLATE_CONVEX		=	(1<<3),

		/**
		\brief Instructs cooking to save normals uncompressed.  The cooked hull data will be larger, but will load faster.

		@see PxCookingParams
		*/
		eUSE_UNCOMPRESSED_NORMALS	=	(1<<5),
	};
};

/**
\brief collection of set bits defined in PxConvexFlag.

@see PxConvexFlag
*/
typedef PxFlags<PxConvexFlag::Enum,PxU16> PxConvexFlags;
PX_FLAGS_OPERATORS(PxConvexFlag::Enum,PxU16);


typedef PxVec3 PxPoint;

/**
\brief Descriptor class for #PxConvexMesh.

@see PxConvexMesh PxConvexMeshGeometry PxShape PxPhysics.createConvexMesh()

*/
class PxConvexMeshDesc
{
public:

	/**
	\brief Pointer to array of vertex positions.
	Pointer to first vertex point. Caller may add pointStrideBytes bytes to the pointer to access the next point.

	<b>Default:</b> NULL
	*/
	PxBoundedData points;

	/**
	\brief Pointer to array of triangle indices.
	<p>Pointer to first triangle. Caller may add triangleStrideBytes bytes to the pointer to access the next triangle.</p>
	<p><pre>These are triplets of 0 based indices:
	vert0 vert1 vert2
	vert0 vert1 vert2
	vert0 vert1 vert2
	...</pre></p>

	<p>Where vertex is either a 32 or 16 bit unsigned integer. There are numTriangles*3 indices.</p>

	<p>This is declared as a void pointer because it is actually either an PxU16 or a PxU32 pointer.</p>

	<b>Default:</b> NULL

	@see PxConvexFlag::e16_BIT_INDICES
	*/
	PxBoundedData triangles;

	/**
	\brief Flags bits, combined from values of the enum ::PxConvexFlag

	<b>Default:</b> 0
	*/
	PxConvexFlags flags;

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxConvexMeshDesc();
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

PX_INLINE PxConvexMeshDesc::PxConvexMeshDesc()	//constructor sets to default
{
}

PX_INLINE void PxConvexMeshDesc::setToDefault()
{
	*this = PxConvexMeshDesc();
}

PX_INLINE bool PxConvexMeshDesc::isValid() const
{
	// Check geometry
	if(points.count < 3 ||	//at least 1 trig's worth of points
		(points.count > 0xffff && flags & PxConvexFlag::e16_BIT_INDICES))
		return false;
	if(!points.data)
		return false;
	if(points.stride < sizeof(PxPoint))	//should be at least one point's worth of data
		return false;

	// Check topology
	// The triangles pointer is not mandatory: the vertex cloud is enough to define the convex hull.
	if(triangles.data)
	{
		// Indexed mesh
		if(triangles.count < 2)	//some algos require at least 2 trigs
			return false;

	    PxU32 limit = (flags & PxConvexFlag::e16_BIT_INDICES) ? sizeof(PxU16)*3 : sizeof(PxU32)*3;
        if(triangles.stride < limit) 
            return false;
	}
	else
	{
		// We can compute the hull from the vertices
		if(!(flags & PxConvexFlag::eCOMPUTE_CONVEX))
			return false;	// If the mesh is convex and we're not allowed to compute the hull,
							// you have to provide it completely (geometry & topology).
	}
	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

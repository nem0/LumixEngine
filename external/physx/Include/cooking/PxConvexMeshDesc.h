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


#ifndef PX_COLLISION_NXCONVEXMESHDESC
#define PX_COLLISION_NXCONVEXMESHDESC
/** \addtogroup cooking
@{
*/

#include "foundation/PxVec3.h"
#include "foundation/PxFlags.h"
#include "common/PxCoreUtilityTypes.h"
#include "geometry/PxConvexMesh.h"

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

		The PhysX libraries assume that the face normal of a triangle with vertices [a,b,c] can be computed as:
		edge1 = b-a
		edge2 = c-a
		face_normal = edge1 x edge2.

		Note: this is the same as counterclockwise winding in a right handed graphics coordinate system.

		If that does not match the winding order for your triangles, raise this flag.

		Deprecated as triangles will not be accepted in future releases as input for convex mesh descriptor.
		*/
		PX_DEPRECATED eFLIPNORMALS		=	(1<<0),

		/**
		Denotes the use of 16-bit vertex indices in PxConvexMeshDesc::triangles or PxConvexMeshDesc::polygons.
		(otherwise, 32-bit indices are assumed)
		@see #PxConvexMeshDesc.triangles
		*/
		e16_BIT_INDICES		=	(1<<1),

		/**
		Automatically recomputes the hull from the vertices. If this flag is not set, you must provide the entire geometry manually.
		*/
		eCOMPUTE_CONVEX		=	(1<<2),	

		/**
		\brief Inflates the convex object according to skin width. If the convex hull computation fails, use this flag to increase robustness.

		\note This flag is only used in combination with eCOMPUTE_CONVEX.

		@see PxCookingParams
		*/
		eINFLATE_CONVEX		=	(1<<3),

		/**
		\brief Checks and removes almost zero-area triangles during convex hull computation. 
		The rejected area size is specified in PxCookingParams::areaTestEpsilon

		\note This flag is only used in combination with eCOMPUTE_CONVEX.

		\note If this flag is used in combination with eINFLATE_CONVEX, the newly added triangles 
		by the inflation algorithm are not checked (size of the triangles depends on PxCooking::skinWidth).  

		@see PxCookingParams PxCookingParams::areaTestEpsilon
		*/		
		eCHECK_ZERO_AREA_TRIANGLES		=	(1<<4)
	};
};

/**
\brief collection of set bits defined in PxConvexFlag.

@see PxConvexFlag
*/
typedef PxFlags<PxConvexFlag::Enum,PxU16> PxConvexFlags;
PX_FLAGS_OPERATORS(PxConvexFlag::Enum,PxU16)


typedef PxVec3 PxPoint;

/**
\brief Descriptor class for #PxConvexMesh.
\note The number of vertices and the number of convex polygons in a cooked convex mesh is limited to 256.

@see PxConvexMesh PxConvexMeshGeometry PxShape PxPhysics.createConvexMesh()

*/
class PxConvexMeshDesc
{
public:

	/**
	\brief Vertex positions data in PxBoundedData format.

	<b>Default:</b> NULL
	*/
	PxBoundedData points;

	/**
	\deprecated
	\brief Triangle indices data in PxBoundedData format.	
	<p><pre>These are triplets of 0 based indices:
	vert0 vert1 vert2
	vert0 vert1 vert2
	vert0 vert1 vert2
	...</pre></p>

	<p>Where vertex is either a 32 or 16 bit unsigned integer. There are numTriangles*3 indices.</p>

	<p>This function is deprecated in favor of creating hulls from polygons directly. 
	To obtain polygons from your triangles use computeHullPolygons.</p>

	<b>Default:</b> NULL

	@see PxConvexFlag::e16_BIT_INDICES
	*/
	PX_DEPRECATED PxBoundedData triangles;

	/**
	\brief Polygons data in PxBoundedData format.
	<p>Pointer to first polygon. </p>

	<b>Default:</b> NULL	

	@see PxHullPolygon
	*/
	PxBoundedData polygons;

	/**
	\brief Polygon indices data in PxBoundedData format.
	<p>Pointer to first index.</p>

	<b>Default:</b> NULL	

	<p>This is declared as a void pointer because it is actually either an PxU16 or a PxU32 pointer.</p>

	@see PxHullPolygon PxConvexFlag::e16_BIT_INDICES
	*/
	PxBoundedData indices;

	/**
	\brief Flags bits, combined from values of the enum ::PxConvexFlag

	<b>Default:</b> 0
	*/
	PxConvexFlags flags;

	/**
	\brief Limits the number of vertices of the result convex mesh. Hard maximum limit is 256 and minimum limit is 4. 

	Please note, that if a vertex limit is used together with the inflation flag, beveling sharp edges in the inflation code
	may cause the limit to be exceeded.

	<b>Default:</b> 256
	*/
	PxU16 vertexLimit;

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
	: vertexLimit(256)
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
		if(polygons.data)
		{
			if(polygons.count < 4) // we require 2 neighbours for each vertex - 4 polygons at least
				return false;

			if(!indices.data) // indices must be provided together with polygons
				return false;

			PxU32 limit = (flags & PxConvexFlag::e16_BIT_INDICES) ? sizeof(PxU16) : sizeof(PxU32);
			if(indices.stride < limit) 
				return false;

			limit = sizeof(PxHullPolygon);
			if(polygons.stride < limit) 
				return false;
		}
		else
		{
			// We can compute the hull from the vertices
			if(!(flags & PxConvexFlag::eCOMPUTE_CONVEX))
				return false;	// If the mesh is convex and we're not allowed to compute the hull,
								// you have to provide it completely (geometry & topology).
		}
	}

	if(vertexLimit < 4 || vertexLimit > 256)
	{
		return false;
	}
	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_NX_CLOTHMESHDESC
#define PX_PHYSICS_NX_CLOTHMESHDESC
/** \addtogroup cooking
@{
*/

#include "common/PxPhysXCommon.h"
#include "foundation/PxVec3.h"
#include "foundation/PxFlags.h"
#include "common/PxCoreUtilityTypes.h"
#include "geometry/PxSimpleTriangleMesh.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Flags which describe how cooking should treat a cloth mesh edge
*/
struct PxClothMeshEdgeFlag
{
	enum Enum
	{
		eQUAD_DIAGONAL = (1<<0)			//!< The edge was only added to convert a quad into triangles (marking these edges allows to reconstruct quads)
	};
};

typedef PxFlags<PxClothMeshEdgeFlag::Enum,PxU32> PxClothMeshEdgeFlags;
PX_FLAGS_OPERATORS(PxClothMeshEdgeFlag::Enum, PxU32);

/**
\brief Flags which describe how cooking should treat a cloth mesh vertex
*/
struct PxClothMeshVertFlag
{
	enum Enum
	{
		eVF_ATTACHED = (1<<0)			//!< The vertex is attached, so the cooker should take the constraint into account.
	};
};

typedef PxFlags<PxClothMeshVertFlag::Enum,PxU32> PxClothMeshVertFlags;
PX_FLAGS_OPERATORS(PxClothMeshVertFlag::Enum, PxU32);


/**
\brief Descriptor class for a cloth mesh.

@see PxCooking.cookClothMesh()

*/
class PxClothMeshDesc : public PxSimpleTriangleMesh
{
public:
	/**
	\brief Edge flags for cooking.

	Information about mesh edge properties can be provided if available. The flags are stored as triples, a triple for each triangle.
	A flag triple is interpreted as follows:

	flag0 flag1 flag2

	for a triangle with vertex indices

	v0, v1, v2

	flag0 => edge v0-v1
	flag1 => edge v1-v2
	flag2 => edge v2-v0
	
	There are numTriangles*3 flags.

	If set to NULL, cooking will analyze the mesh and create the flags based on the direction of gravity and some heuristics.
	*/
	const PxClothMeshEdgeFlags* edgeFlags;

	/**
	\brief Vertex flags for cooking.

	Information about mesh vertex properties can be provided if available.
	The flags are assigned per each vertex, so the size of this array should be the same as
	number of particles in the mesh.

	If set to NULL, cooking will ignore this flag.
	*/
	const PxClothMeshVertFlags* vertFlags;

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
	PxSimpleTriangleMesh::setToDefault();
	edgeFlags = NULL;
	vertFlags = NULL;
}

PX_INLINE void PxClothMeshDesc::setToDefault()
{
	*this = PxClothMeshDesc();
}

PX_INLINE bool PxClothMeshDesc::isValid() const
{
	if(points.count < 3) 	//at least 1 trig's worth of points
		return false;
	if (!triangles.data)	// no support for non-indexed mesh
		return false;

	return PxSimpleTriangleMesh::isValid();
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

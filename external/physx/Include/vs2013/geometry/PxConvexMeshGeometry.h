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


#ifndef PX_PHYSICS_NX_CONVEXMESH_GEOMETRY
#define PX_PHYSICS_NX_CONVEXMESH_GEOMETRY
/** \addtogroup geomutils
@{
*/
#include "geometry/PxGeometry.h"
#include "geometry/PxMeshScale.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxConvexMesh;

/**
\brief Convex mesh geometry class.

This class unifies a convex mesh object with a scaling transform, and 
lets the combined object be used anywhere a PxGeometry is needed.

The scaling is a transform along arbitrary axes contained in the scale object.
The vertices of the mesh in geometry (or shape) space is the 
PxMeshScale::toMat33() transform, multiplied by the vertex space vertices 
in the PxConvexMesh object.
*/
class PxConvexMeshGeometry : public PxGeometry 
{
public:
	/**
	\brief Default constructor.

	Creates an empty object with a NULL mesh and identity scale.
	*/
	PX_INLINE PxConvexMeshGeometry() :
		PxGeometry(PxGeometryType::eCONVEXMESH),
		scale(PxMeshScale(1.0f)),
		convexMesh(NULL)
	{}

		/**
		\brief Constructor.
		\param[in] mesh The Mesh pointer.  May be NULL, though this will not make the object valid for shape construction.
		\param[in] scaling The scale factor.
		\
		*/
	PX_INLINE PxConvexMeshGeometry(PxConvexMesh* mesh, 
								   const PxMeshScale& scaling = PxMeshScale()) :
		PxGeometry(PxGeometryType::eCONVEXMESH),
		scale(scaling),
		convexMesh(mesh)
	{}

	/**
	\brief Returns true if the geometry is valid.

	\return True if the current settings are valid for shape creation.

	\note A valid convex mesh has a positive scale value in each direction (scale.x > 0, scale.y > 0, scale.z > 0).
	It is illegal to call PxRigidActor::createShape and PxPhysics::createShape with a convex that has zero extent in any direction.

	@see PxRigidActor::createShape, PxPhysics::createShape
	*/
	PX_INLINE bool isValid() const;

public:
	
	/**
	\brief Scale factor that transforms from vertex space to shape space.
	*/
	PxMeshScale		scale;	
	/**
	\brief The mesh data in vertex space.
	*/
	PxConvexMesh*	convexMesh;
};


PX_INLINE bool PxConvexMeshGeometry::isValid() const
{
	if (mType != PxGeometryType::eCONVEXMESH)
		return false;
	if (!scale.scale.isFinite() || !scale.rotation.isUnit())
		return false;
	if (scale.scale.x <= 0.0f || scale.scale.y <= 0.0f || scale.scale.z <= 0.0f)
		return false;
	if (!convexMesh)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

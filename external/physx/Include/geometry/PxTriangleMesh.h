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


#ifndef PX_PHYSICS_GEOMUTILS_NX_TRIANGLEMESH
#define PX_PHYSICS_GEOMUTILS_NX_TRIANGLEMESH
/** \addtogroup geomutils 
@{ */

#include "foundation/PxVec3.h"
#include "foundation/PxBounds3.h"
#include "geometry/PxPhysXGeomUtils.h"
#include "common/PxSerialFramework.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**

\brief A triangle mesh, also called a 'polygon soup'.

It is represented as an indexed triangle list. There are no restrictions on the
triangle data. 

To avoid duplicating data when you have several instances of a particular 
mesh positioned differently, you do not use this class to represent a 
mesh object directly. Instead, you create an instance of this mesh via
the PxTriangleMeshGeometry and PxShape classes.

<h3>Creation</h3>

To create an instance of this class call PxPhysics::createTriangleMesh(),
and release() to delete it. This is only possible
once you have released all of its PxShape instances.


<h3>Visualizations:</h3>
\li #PxVisualizationParameter::eCOLLISION_AABBS
\li #PxVisualizationParameter::eCOLLISION_SHAPES
\li #PxVisualizationParameter::eCOLLISION_AXES
\li #PxVisualizationParameter::eCOLLISION_FNORMALS
\li #PxVisualizationParameter::eCOLLISION_EDGES

@see PxTriangleMeshDesc PxTriangleMeshGeometry PxShape PxPhysics.createTriangleMesh()
*/

class PxTriangleMesh : public PxSerializable
{
	public:
	/**
	\brief Returns the number of vertices.
	\return	number of vertices
	@see getVertices()
	*/
	PX_PHYSX_COMMON_API virtual	PxU32				getNbVertices()									const	= 0;

	/**
	\brief Returns the vertices.
	\return	array of vertices
	@see getNbVertices()
	*/
	PX_PHYSX_COMMON_API virtual	const PxVec3*		getVertices()									const	= 0;

	/**
	\brief Returns the number of triangles.
	\return	number of triangles
	@see getTriangles() has16BitTriangleIndices() getTrianglesRemap()
	*/
	PX_PHYSX_COMMON_API virtual	PxU32				getNbTriangles()								const	= 0;

	/**
	\brief Returns the triangle indices.

	The indices can be 16 or 32bit depending on the number of triangles in the mesh.
	Call has16BitTriangleIndices() to know if the indices are 16 or 32 bits.

	The number of indices is the number of triangles * 3.

	\return	array of triangles
	@see getNbTriangles() has16BitTriangleIndices() getTrianglesRemap()
	*/
	PX_PHYSX_COMMON_API virtual	const void*			getTriangles()									const	= 0;

	/**
	\brief Returns whether the triangle indices are 16 or 32bits.

	\return	true for 16bit indices, false for 32bit indices
	@see getNbTriangles() getTriangles() getTrianglesRemap()
	*/
	PX_PHYSX_COMMON_API virtual	bool				has16BitTriangleIndices()						const	= 0;

	/**
	\brief Returns the triangle remapping table.

	The triangles are internally sorted according to various criteria. Hence the internal triangle order
	does not always match the original (user-defined) order. The remapping table helps finding the old
	indices knowing the new ones:

		remapTable[ internalTriangleIndex ] = originalTriangleIndex

	\return	the remapping table
	@see getNbTriangles() getTriangles() has16BitTriangleIndices()
	*/
	PX_PHYSX_COMMON_API virtual	const PxU32*		getTrianglesRemap()								const	= 0;


	/**
	\brief Destroys the triangle mesh.

	\note This will decrease the reference count by one.

	Releases the application's reference to the triangle mesh.
	The mesh is destroyed when the application's reference is released and all shapes referencing the mesh are destroyed.
	
	@see PxPhysics.createTriangleMesh()
	*/
	PX_PHYSX_COMMON_API virtual void				release() = 0;

	/**
	\brief Returns material table index of given triangle

	This function takes a post cooking triangle index.

	\param[in] triangleIndex (internal) index of desired triangle
	\return Material table index, or 0xffff if no per-triangle materials are used
	*/
	PX_PHYSX_COMMON_API virtual	PxMaterialTableIndex	getTriangleMaterialIndex(PxTriangleID triangleIndex)	const	= 0;

	/**
	\brief Returns the local-space (vertex space) AABB from the triangle mesh.

	\return	local-space bounds
	*/
	PX_PHYSX_COMMON_API virtual	PxBounds3			getLocalBounds()	const	= 0;

	/**
	\brief Returns the reference count for shared meshes.

	At creation, the reference count of the mesh is 1. Every shape referencing this mesh increments the
	count by 1.	When the reference count reaches 0, and only then, the mesh gets destroyed automatically.

	\return the current reference count.
	*/
	PX_PHYSX_COMMON_API virtual PxU32				getReferenceCount()			const	= 0;

	PX_INLINE virtual	const char*			getConcreteTypeName() const					{	return "PxTriangleMesh"; }

protected:
	PxTriangleMesh()										{}
	PxTriangleMesh(PxRefResolver& v)	: PxSerializable(v)	{}

	virtual ~PxTriangleMesh(){}
	virtual	bool				isKindOf(const char* name)	const		{	return !strcmp("PxTriangleMesh", name) || PxSerializable::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

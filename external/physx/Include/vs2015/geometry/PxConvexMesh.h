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


#ifndef PX_PHYSICS_GEOMUTILS_NX_CONVEXMESH
#define PX_PHYSICS_GEOMUTILS_NX_CONVEXMESH
/** \addtogroup geomutils
  @{
*/

#include "foundation/Px.h"
#include "common/PxBase.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Polygon data

Plane format: (mPlane[0],mPlane[1],mPlane[2]).dot(x) + mPlane[3] = 0
With the normal outward-facing from the hull.
*/
struct PxHullPolygon
{
	PxReal			mPlane[4];		//!< Plane equation for this polygon
	PxU16			mNbVerts;		//!< Number of vertices/edges in the polygon
	PxU16			mIndexBase;		//!< Offset in index buffer
};

/**
\brief A convex mesh.

Internally represented as a list of convex polygons. The number
of polygons is limited to 256.

To avoid duplicating data when you have several instances of a particular
mesh positioned differently, you do not use this class to represent a
convex object directly. Instead, you create an instance of this mesh via
the PxConvexMeshGeometry and PxShape classes.

<h3>Creation</h3>

To create an instance of this class call PxPhysics::createConvexMesh(),
and PxConvexMesh::release() to delete it. This is only possible
once you have released all of its #PxShape instances.

<h3>Visualizations:</h3>
\li #PxVisualizationParameter::eCOLLISION_AABBS
\li #PxVisualizationParameter::eCOLLISION_SHAPES
\li #PxVisualizationParameter::eCOLLISION_AXES
\li #PxVisualizationParameter::eCOLLISION_FNORMALS
\li #PxVisualizationParameter::eCOLLISION_EDGES

@see PxConvexMeshDesc PxPhysics.createConvexMesh()
*/
class PxConvexMesh	: public PxBase
{
public:

	/**
	\brief Returns the number of vertices.
	\return	Number of vertices.
	@see getVertices()
	*/
	PX_PHYSX_COMMON_API virtual	PxU32				getNbVertices()									const	= 0;

	/**
	\brief Returns the vertices.
	\return	Array of vertices.
	@see getNbVertices()
	*/
	PX_PHYSX_COMMON_API virtual	const PxVec3*		getVertices()									const	= 0;

	/**
	\brief Returns the index buffer.
	\return	Index buffer.
	@see getNbPolygons() getPolygonData()
	*/
	PX_PHYSX_COMMON_API virtual	const PxU8*			getIndexBuffer()								const	= 0;

	/**
	\brief Returns the number of polygons.
	\return	Number of polygons.
	@see getIndexBuffer() getPolygonData()
	*/
	PX_PHYSX_COMMON_API virtual	PxU32				getNbPolygons()									const	= 0;

	/**
	\brief Returns the polygon data.
	\param[in] index	Polygon index in [0 ; getNbPolygons()[.
	\param[out] data	Polygon data.
	\return	True if success.
	@see getIndexBuffer() getNbPolygons()
	*/
	PX_PHYSX_COMMON_API virtual	bool				getPolygonData(PxU32 index, PxHullPolygon& data)	const	= 0;

	/**
	\brief Decrements the reference count of a convex mesh and releases it if the new reference count is zero.	
	
	The mesh is destroyed when the application's reference is released and all shapes referencing the mesh are destroyed.

	@see PxPhysics.createConvexMesh() PxConvexMeshGeometry PxShape
	*/
	PX_PHYSX_COMMON_API virtual	void				release() = 0;

	/**
	\brief Returns the reference count for shared meshes.

	At creation, the reference count of the convex mesh is 1. Every shape referencing this convex mesh increments the
	count by 1.	When the reference count reaches 0, and only then, the convex mesh gets destroyed automatically.

	\return the current reference count.
	*/
	PX_PHYSX_COMMON_API virtual PxU32				getReferenceCount()			const	= 0;

	/**
	\brief Returns the mass properties of the mesh assuming unit density.

	The following relationship holds between mass and volume:

		mass = volume * density

	The mass of a unit density mesh is equal to its volume, so this function returns the volume of the mesh.

	Similarly, to obtain the localInertia of an identically shaped object with a uniform density of d, simply multiply the
	localInertia of the unit density mesh by d.

	\param[out] mass The mass of the mesh assuming unit density.
	\param[out] localInertia The inertia tensor in mesh local space assuming unit density.
	\param[out] localCenterOfMass Position of center of mass (or centroid) in mesh local space.
	*/
	PX_PHYSX_COMMON_API virtual void				getMassInformation(PxReal& mass, PxMat33& localInertia, PxVec3& localCenterOfMass)		const	= 0;

	/**
	\brief Returns the local-space (vertex space) AABB from the convex mesh.

	\return	local-space bounds
	*/
	PX_PHYSX_COMMON_API virtual	PxBounds3			getLocalBounds()	const	= 0;

	PX_PHYSX_COMMON_API virtual	const char*			getConcreteTypeName() const	{ return "PxConvexMesh"; }

protected:
						PX_INLINE					PxConvexMesh(PxType concreteType, PxBaseFlags baseFlags) : PxBase(concreteType, baseFlags) {}
						PX_INLINE					PxConvexMesh(PxBaseFlags baseFlags) : PxBase(baseFlags) {}
	PX_PHYSX_COMMON_API virtual						~PxConvexMesh() {}
	PX_PHYSX_COMMON_API virtual	bool				isKindOf(const char* name) const { return !strcmp("PxConvexMesh", name) || PxBase::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_NX_BOX_GEOMETRY
#define PX_PHYSICS_NX_BOX_GEOMETRY
/** \addtogroup geomutils
@{
*/
#include "geometry/PxGeometry.h"
#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Class representing the geometry of a box.  

The geometry of a box can be fully specified by its half extents.  This is the half of its width, height, and depth.
\note The scaling of the box is expected to be baked into these values, there is no additional scaling parameter.
*/
class PxBoxGeometry : public PxGeometry 
{
public:
	/**
	\brief Default constructor, initializes to a box with zero dimensions.
	*/
	PX_INLINE PxBoxGeometry() :									PxGeometry(PxGeometryType::eBOX), halfExtents(0,0,0)		{}
	/**
	\brief Copy constructor.
	*/
	PX_INLINE PxBoxGeometry(const PxBoxGeometry& b) :			PxGeometry(PxGeometryType::eBOX), halfExtents(b.halfExtents){}
	/**
	\brief Constructor to initialize half extents from scalar parameters.
	\param hx Initial half extents' x component.
	\param hy Initial half extents' y component.
	\param hz Initial half extents' z component.
	*/
	PX_INLINE PxBoxGeometry(PxReal hx, PxReal hy, PxReal hz) :	PxGeometry(PxGeometryType::eBOX), halfExtents(hx, hy, hz)	{}

	/**
	\brief Constructor to initialize half extents from vector parameter.
	\param halfExtents_ Initial half extents.
	*/
	PX_INLINE PxBoxGeometry(PxVec3 halfExtents_) :				PxGeometry(PxGeometryType::eBOX), halfExtents(halfExtents_)	{}

	/**
	\brief Returns true if the geometry is valid.

	\return True if the current settings are valid

	\note A valid box has a positive extent in each direction (halfExtents.x > 0, halfExtents.y > 0, halfExtents.z > 0). 
	It is illegal to call PxRigidActor::createShape and PxPhysics::createShape with a box that has zero extent in any direction.

	@see PxRigidActor::createShape, PxPhysics::createShape
	*/
	PX_INLINE bool isValid() const;

public:
	/**
	\brief Half of the width, height, and depth of the box.
	*/
	PxVec3 halfExtents;
};


PX_INLINE bool PxBoxGeometry::isValid() const
{
	if (mType != PxGeometryType::eBOX)
		return false;
	if (!halfExtents.isFinite())
		return false;
	if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

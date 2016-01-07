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


#ifndef PX_PHYSICS_NX_HEIGHTFIELD_GEOMETRY
#define PX_PHYSICS_NX_HEIGHTFIELD_GEOMETRY
/** \addtogroup geomutils
@{
*/
#include "geometry/PxTriangleMeshGeometry.h"
#include "common/PxCoreUtilityTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

#define PX_MIN_HEIGHTFIELD_XZ_SCALE 1e-8f
#define PX_MIN_HEIGHTFIELD_Y_SCALE (0.0001f / PxReal(0xFFFF))

class PxHeightField;

/**
\brief Height field geometry class.

This class allows to create a scaled height field geometry instance.

There is a minimum allowed value for Y and XZ scaling - PX_MIN_HEIGHTFIELD_XZ_SCALE, heightfield creation will fail if XZ value is below this value.
*/
class PxHeightFieldGeometry : public PxGeometry 
{
public:
	PX_INLINE PxHeightFieldGeometry() :		
		PxGeometry(PxGeometryType::eHEIGHTFIELD),
		heightField(NULL),
		heightScale(1.0f), 
		rowScale(1.0f), 
		columnScale(1.0f), 
		heightFieldFlags(0)
	{}

	PX_INLINE PxHeightFieldGeometry(PxHeightField* hf,
									PxMeshGeometryFlags flags, 
									PxReal heightscale,
									PxReal rowscale, 
									PxReal columnscale) :
		PxGeometry			(PxGeometryType::eHEIGHTFIELD), 
		heightField			(hf) ,
		heightScale			(heightscale), 
		rowScale			(rowscale), 
		columnScale			(columnscale), 
		heightFieldFlags	(flags)
		{
		}

	/**
	\brief Returns true if the geometry is valid.

	\return True if the current settings are valid

	\note A valid height field has a positive scale value in each direction (heightScale > 0, rowScale > 0, columnScale > 0).
	It is illegal to call PxRigidActor::createShape and PxPhysics::createShape with a height field that has zero extents in any direction.

	@see PxRigidActor::createShape, PxPhysics::createShape
	*/
	PX_INLINE bool isValid() const;

public:
	/**
	\brief The height field data.
	*/
	PxHeightField*			heightField;

	/**
	\brief The scaling factor for the height field in vertical direction (y direction in local space).
	*/
	PxReal					heightScale;

	/**
	\brief The scaling factor for the height field in the row direction (x direction in local space).
	*/
	PxReal					rowScale;

	/**
	\brief The scaling factor for the height field in the column direction (z direction in local space).
	*/
	PxReal					columnScale;

	/**
	\brief Flags to specify some collision properties for the height field.
	*/
	PxMeshGeometryFlags		heightFieldFlags;

	PxPadding<3>			paddingFromFlags;	//< padding for mesh flags.
};


PX_INLINE bool PxHeightFieldGeometry::isValid() const
{
	if (mType != PxGeometryType::eHEIGHTFIELD)
		return false;
	if (!PxIsFinite(heightScale) || !PxIsFinite(rowScale) || !PxIsFinite(columnScale))
		return false;
	if (rowScale < PX_MIN_HEIGHTFIELD_XZ_SCALE || columnScale < PX_MIN_HEIGHTFIELD_XZ_SCALE || heightScale < PX_MIN_HEIGHTFIELD_Y_SCALE)
		return false;
	if (!heightField)
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

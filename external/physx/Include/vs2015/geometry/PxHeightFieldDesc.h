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


#ifndef PX_COLLISION_NXHEIGHTFIELDDESC
#define PX_COLLISION_NXHEIGHTFIELDDESC
/** \addtogroup geomutils
@{
*/

#include "foundation/PxBounds3.h"
#include "common/PxPhysXCommonConfig.h"
#include "geometry/PxHeightFieldFlag.h"
#include "common/PxCoreUtilityTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Descriptor class for #PxHeightField.

\note The heightfield data is *copied* when a PxHeightField object is created from this descriptor. After the call the
user may discard the height data.

@see PxHeightField PxHeightFieldGeometry PxShape PxPhysics.createHeightField()
*/
class PxHeightFieldDesc
{
public:

	/**
	\brief Number of sample rows in the height field samples array.

	\note Local space X-axis corresponds to rows.

	<b>Range:</b> &gt;1<br>
	<b>Default:</b> 0
	*/
	PxU32							nbRows;

	/**
	\brief Number of sample columns in the height field samples array.

	\note Local space Z-axis corresponds to columns.

	<b>Range:</b> &gt;1<br>
	<b>Default:</b> 0
	*/
	PxU32							nbColumns;

	/**
	\brief Format of the sample data.

	Currently the only supported format is PxHeightFieldFormat::eS16_TM:

	<b>Default:</b> PxHeightFieldFormat::eS16_TM

	@see PxHeightFormat PxHeightFieldDesc.samples
	*/
	PxHeightFieldFormat::Enum		format;

	/**
	\brief The samples array.

	It is copied to the SDK's storage at creation time.

	There are nbRows * nbColumn samples in the array,
	which define nbRows * nbColumn vertices and cells,
	of which (nbRows - 1) * (nbColumns - 1) cells are actually used.

	The array index of sample(row, column) = row * nbColumns + column.
	The byte offset of sample(row, column) = sampleStride * (row * nbColumns + column).
	The sample data follows at the offset and spans the number of bytes defined by the format.
	Then there are zero or more unused bytes depending on sampleStride before the next sample.

	<b>Default:</b> NULL

	@see PxHeightFormat
	*/
	PxStridedData					samples;

	/**
	\brief Sets how thick the heightfield surface is.

	In this way even objects which are under the surface of the height field but above
	this cutoff are treated as colliding with the height field.

	The thickness is measured relative to the surface at the given point.

	You may set this to a positive value, in which case the extent will be cast along the opposite side of the height field.

	You may use a smaller finite value for the extent if you want to put some space under the height field, such as a cave.

	\note Please refer to the Raycasts Against Heightfields section of the user guide for details of how this value affects raycasts.

	<b>Range:</b> (-PX_MAX_BOUNDS_EXTENTS, PX_MAX_BOUNDS_EXTENTS)<br>
	<b>Default:</b> -1
	*/
	PxReal					thickness;

	/**
	This threshold is used by the collision detection to determine if a height field edge is convex
	and can generate contact points.
	Usually the convexity of an edge is determined from the angle (or cosine of the angle) between
	the normals of the faces sharing that edge.
	The height field allows a more efficient approach by comparing height values of neighboring vertices.
	This parameter offsets the comparison. Smaller changes than 0.5 will not alter the set of convex edges.
	The rule of thumb is that larger values will result in fewer edge contacts.

	This parameter is ignored in contact generation with sphere and capsule primitives.

	<b>Range:</b> [0, PX_MAX_F32)<br>
	<b>Default:</b> 0
	*/
	PxReal					convexEdgeThreshold;

	/**
	\brief Flags bits, combined from values of the enum ::PxHeightFieldFlag.

	<b>Default:</b> 0

	@see PxHeightFieldFlag PxHeightFieldFlags
	*/
	PxHeightFieldFlags		flags;

	/**
	\brief Constructor sets to default.
	*/
	PX_INLINE				PxHeightFieldDesc();

	/**
	\brief (re)sets the structure to the default.
	*/
	PX_INLINE		void	setToDefault();

	/**
	\brief Returns true if the descriptor is valid.
	\return True if the current settings are valid.
	*/
	PX_INLINE		bool	isValid() const;
};

PX_INLINE PxHeightFieldDesc::PxHeightFieldDesc()	//constructor sets to default
{
	nbColumns					= 0;
	nbRows						= 0;
	format						= PxHeightFieldFormat::eS16_TM;
	thickness					= -1.0f;
	convexEdgeThreshold			= 0.0f;
	flags						= PxHeightFieldFlags();
}

PX_INLINE void PxHeightFieldDesc::setToDefault()
{
	*this = PxHeightFieldDesc();
}

PX_INLINE bool PxHeightFieldDesc::isValid() const
{
	if (nbColumns < 2)
		return false;
	if (nbRows < 2)
		return false;
	switch (format)
	{
	case PxHeightFieldFormat::eS16_TM:
		if (samples.stride < 4)
			return false;
		break;
	default:
		return false;
	}
	if (convexEdgeThreshold < 0)
		return false;
	if ((flags & PxHeightFieldFlag::eNO_BOUNDARY_EDGES) != flags)
		return false;
	if (thickness < -PX_MAX_BOUNDS_EXTENTS || thickness > PX_MAX_BOUNDS_EXTENTS)
		return false;
	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

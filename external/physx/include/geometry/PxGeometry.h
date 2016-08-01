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


#ifndef PX_PHYSICS_NX_GEOMETRY
#define PX_PHYSICS_NX_GEOMETRY
/** \addtogroup geomutils
@{
*/

#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxFlags.h"
#include "foundation/PxMath.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A geometry type.

Used to distinguish the type of a ::PxGeometry object.
*/
struct PxGeometryType
{
	enum Enum
	{
		eSPHERE,
		ePLANE,
		eCAPSULE,
		eBOX,
		eCONVEXMESH,
		eTRIANGLEMESH,
		eHEIGHTFIELD,

		eGEOMETRY_COUNT,	//!< internal use only!
		eINVALID = -1		//!< internal use only!
	};
};

/**
\brief A geometry object.

A geometry object defines the characteristics of a spatial object, but without any information
about its placement in the world.

\note This is an abstract class.  You cannot create instances directly.  Create an instance of one of the derived classes instead.
*/
class PxGeometry 
{ 
public:
	/**
	\brief Returns the type of the geometry.
	\return The type of the object.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxGeometryType::Enum getType() const	{ return mType; }	

protected:
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxGeometry(PxGeometryType::Enum type) : mType(type) {}
	PxGeometryType::Enum mType; 
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

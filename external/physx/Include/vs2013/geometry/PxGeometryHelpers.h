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


#ifndef PX_PHYSICS_GEOMETRYHELPERS
#define PX_PHYSICS_GEOMETRYHELPERS
/** \addtogroup geomutils
@{
*/

#include "common/PxPhysXCommonConfig.h"
#include "PxGeometry.h"
#include "PxBoxGeometry.h"
#include "PxSphereGeometry.h"
#include "PxCapsuleGeometry.h"
#include "PxPlaneGeometry.h"
#include "PxConvexMeshGeometry.h"
#include "PxTriangleMeshGeometry.h"
#include "PxHeightFieldGeometry.h"
#include "foundation/PxPlane.h"
#include "foundation/PxTransform.h"
#include "foundation/PxUnionCast.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Geometry holder class

This class contains enough space to hold a value of any PxGeometry subtype.

Its principal use is as a convenience class to allow geometries to be returned polymorphically 
from functions. See PxShape::getGeometry();
*/

PX_ALIGN_PREFIX(4)
class PxGeometryHolder
{
public:
	PX_FORCE_INLINE PxGeometryType::Enum getType() const
	{
		return any().getType();
	}

	PX_FORCE_INLINE PxGeometry& any()
	{ 
		return *PxUnionCast<PxGeometry*>(&bytes.geometry); 
	}

	PX_FORCE_INLINE const PxGeometry& any() const 
	{ 
		return *PxUnionCast<const PxGeometry*>(&bytes.geometry); 
	}

	PX_FORCE_INLINE PxSphereGeometry& sphere()
	{
		return get<PxSphereGeometry, PxGeometryType::eSPHERE>();
	}

	PX_FORCE_INLINE const PxSphereGeometry& sphere() const
	{
		return get<const PxSphereGeometry, PxGeometryType::eSPHERE>();
	}

	PX_FORCE_INLINE PxPlaneGeometry& plane()
	{
		return get<PxPlaneGeometry, PxGeometryType::ePLANE>();
	}

	PX_FORCE_INLINE const PxPlaneGeometry& plane() const
	{
		return get<const PxPlaneGeometry, PxGeometryType::ePLANE>();
	}


	PX_FORCE_INLINE PxCapsuleGeometry& capsule()
	{
		return get<PxCapsuleGeometry, PxGeometryType::eCAPSULE>();
	}

	PX_FORCE_INLINE const PxCapsuleGeometry& capsule() const
	{
		return get<const PxCapsuleGeometry, PxGeometryType::eCAPSULE>();
	}


	PX_FORCE_INLINE PxBoxGeometry& box()
	{
		return get<PxBoxGeometry, PxGeometryType::eBOX>();
	}

	PX_FORCE_INLINE const PxBoxGeometry& box() const
	{
		return get<const PxBoxGeometry, PxGeometryType::eBOX>();
	}

	PX_FORCE_INLINE PxConvexMeshGeometry& convexMesh()
	{
		return get<PxConvexMeshGeometry, PxGeometryType::eCONVEXMESH>();
	}

	PX_FORCE_INLINE const PxConvexMeshGeometry& convexMesh() const
	{
		return get<const PxConvexMeshGeometry, PxGeometryType::eCONVEXMESH>();
	}


	PX_FORCE_INLINE PxTriangleMeshGeometry& triangleMesh()
	{
		return get<PxTriangleMeshGeometry, PxGeometryType::eTRIANGLEMESH>();
	}

	PX_FORCE_INLINE const PxTriangleMeshGeometry& triangleMesh() const
	{
		return get<const PxTriangleMeshGeometry, PxGeometryType::eTRIANGLEMESH>();
	}

	PX_FORCE_INLINE PxHeightFieldGeometry& heightField()
	{
		return get<PxHeightFieldGeometry, PxGeometryType::eHEIGHTFIELD>();
	}

	PX_FORCE_INLINE const PxHeightFieldGeometry& heightField() const
	{
		return get<const PxHeightFieldGeometry, PxGeometryType::eHEIGHTFIELD>();
	}

	PX_FORCE_INLINE void storeAny(const PxGeometry& geometry)
	{
		switch(geometry.getType())
		{
		case PxGeometryType::eSPHERE:		put<PxSphereGeometry>(geometry); break;
		case PxGeometryType::ePLANE:		put<PxPlaneGeometry>(geometry); break;
		case PxGeometryType::eCAPSULE:		put<PxCapsuleGeometry>(geometry); break;
		case PxGeometryType::eBOX:			put<PxBoxGeometry>(geometry); break;
		case PxGeometryType::eCONVEXMESH:	put<PxConvexMeshGeometry>(geometry); break;
		case PxGeometryType::eTRIANGLEMESH: put<PxTriangleMeshGeometry>(geometry); break;
		case PxGeometryType::eHEIGHTFIELD:	put<PxHeightFieldGeometry>(geometry); break;
		case PxGeometryType::eGEOMETRY_COUNT:
		case PxGeometryType::eINVALID:
		default:
			PX_ASSERT(0);
		}
	}

	private:
		template<typename T> void put(const PxGeometry& geometry)
		{
			static_cast<T&>(any()) = static_cast<const T&>(geometry);
		}


		template<typename T, PxGeometryType::Enum type> T& get()
		{
			PX_ASSERT(getType() == type);
			return static_cast<T&>(any());
		}

		template<typename T, PxGeometryType::Enum type> T& get() const
		{
			PX_ASSERT(getType() == type);
			return static_cast<T&>(any());
		}


	union {
		PxU8	geometry[sizeof(PxGeometry)];
		PxU8	box[sizeof(PxBoxGeometry)];
		PxU8	sphere[sizeof(PxSphereGeometry)];
		PxU8	capsule[sizeof(PxCapsuleGeometry)];
		PxU8	plane[sizeof(PxPlaneGeometry)];
		PxU8	convex[sizeof(PxConvexMeshGeometry)];
		PxU8	mesh[sizeof(PxTriangleMeshGeometry)];
		PxU8	heightfield[sizeof(PxHeightFieldGeometry)];
	} bytes;
}
PX_ALIGN_SUFFIX(4);




#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

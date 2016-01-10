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


#ifndef PX_PHYSICS_NX_MESHSCALE
#define PX_PHYSICS_NX_MESHSCALE
/** \addtogroup geomutils
@{
*/

#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxMat33.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A class expressing a nonuniform scaling transformation.

The scaling is along arbitrary axes that are specified by PxMeshScale::rotation.

\note Currently only positive scale values are supported.

@see PxConvexMeshGeometry PxTriangleMeshGeometry
*/
class PxMeshScale
{
//= ATTENTION! =====================================================================================
// Changing the data layout of this class breaks the binary serialization format.  See comments for 
// PX_BINARY_SERIAL_VERSION.  If a modification is required, please adjust the getBinaryMetaData 
// function.  If the modification is made on a custom branch, please change PX_BINARY_SERIAL_VERSION
// accordingly.
//==================================================================================================
public:
	/**
	\brief Constructor initializes to identity scale.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxMeshScale(): scale(1.0f), rotation(PxIdentity) 
	{
	}

	/**
	\brief Constructor from scalar.
	*/
	explicit PX_CUDA_CALLABLE PX_FORCE_INLINE PxMeshScale(PxReal r): scale(r), rotation(PxIdentity) 
	{
	}

	/**
	\brief Constructor to initialize to arbitrary scaling.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxMeshScale(const PxVec3& s, const PxQuat& r)
	{
		PX_ASSERT(r.isUnit());
		scale = s;
		rotation = r;
	}


	/**
	\brief Returns true if the scaling is an identity transformation.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE bool isIdentity()	const
	{
		return (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f);
	}

	/**
	\brief Returns the inverse of this scaling transformation.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE  PxMeshScale getInverse() const 
	{
		return PxMeshScale(PxVec3(1.0f/scale.x, 1.0f/scale.y, 1.0f/scale.z), rotation);
	}

	/**
	\deprecated
	\brief Returns the identity scaling transformation.
	*/
	PX_DEPRECATED static PX_CUDA_CALLABLE PX_FORCE_INLINE PxMeshScale createIdentity()
	{
		return PxMeshScale(1.0f);
	}

	/**
	\brief Converts this transformation to a 3x3 matrix representation.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxMat33 toMat33() const 
	{
		PxMat33 rot(rotation);
		PxMat33 trans = rot.getTranspose();
		trans.column0 *= scale[0];
		trans.column1 *= scale[1];
		trans.column2 *= scale[2];
		return trans * rot;
	}


	PxVec3		transform(const PxVec3& v) const
	{
		return rotation.rotateInv(scale.multiply(rotation.rotate(v)));
	}

	PxVec3		scale;		//!< A nonuniform scaling
	PxQuat		rotation;	//!< The orientation of the scaling axes


};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

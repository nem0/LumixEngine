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


#ifndef PX_PHYSICS_EXTENSIONS_CLOTH_FABRIC_COOKER_H
#define PX_PHYSICS_EXTENSIONS_CLOTH_FABRIC_COOKER_H

/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "PxClothMeshDesc.h"
#include "cloth/PxClothFabric.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPhysics;

struct PxFabricCookerImpl;

class PxClothFabricCooker
{
public:
	/**
	\brief Cooks a triangle mesh to a PxClothFabricDesc.
	\param desc The cloth mesh descriptor on which the generation of the cooked mesh depends.
	\param gravity A normalized vector which specifies the direction of gravity. 
	This information allows the cooker to generate a fabric with higher quality simulation behavior.
	\param useGeodesicTether A flag to indicate whether to compute geodesic distance for tether constraints.
	\note The geodesic option for tether only works for manifold input.  For non-manifold input, a simple Euclidean distance will be used.
	For more detailed cooker status for such cases, try running PxClothGeodesicTetherCooker directly.
	*/
	PxClothFabricCooker(const PxClothMeshDesc& desc, const PxVec3& gravity, bool useGeodesicTether = true);
	~PxClothFabricCooker();

	/** \brief Returns the fabric descriptor to create the fabric. */
	PxClothFabricDesc getDescriptor() const;
	/** \brief Saves the fabric data to a platform and version dependent stream. */
	void save(PxOutputStream& stream, bool platformMismatch) const;

private:
	PxFabricCookerImpl* mImpl;
};

/**
\brief Cooks a triangle mesh to a PxClothFabric.

\param physics The physics instance.
\param desc The cloth mesh descriptor on which the generation of the cooked mesh depends.
\param gravity A normalized vector which specifies the direction of gravity. 
This information allows the cooker to generate a fabric with higher quality simulation behavior.
\param useGeodesicTether A flag to indicate whether to compute geodesic distance for tether constraints.
\return The created cloth fabric, or NULL if creation failed.
*/
PxClothFabric* PxClothFabricCreate(PxPhysics& physics, 
	const PxClothMeshDesc& desc, const PxVec3& gravity, bool useGeodesicTether = true);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_PHYSICS_EXTENSIONS_CLOTH_FABRIC_COOKER_H

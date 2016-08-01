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


#ifndef PX_PHYSICS_EXTENSIONS_CLOTH_EDGE_QUADIFIER_H
#define PX_PHYSICS_EXTENSIONS_CLOTH_EDGE_QUADIFIER_H

#include "common/PxPhysXCommonConfig.h"
#include "PxClothMeshDesc.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	
struct PxClothMeshQuadifierImpl;

class PxClothMeshQuadifier
{
public:
	/**
	\brief Convert triangles of PxClothMeshDesc to quads.
	\details In PxCloth, quad dominant mesh representations are preferable to pre-triangulated versions.
	In cases where the mesh has been already triangulated, this class provides a meachanism to
	convert (quadify) some triangles back to quad representations.
	\see PxClothFabricCooker
	\param desc The cloth mesh descriptor prepared for cooking
	*/
	PxClothMeshQuadifier(const PxClothMeshDesc &desc);
	~PxClothMeshQuadifier();

    /** 
	\brief Returns a mesh descriptor with some triangle pairs converted to quads.
	\note The returned descriptor is valid only within the lifespan of PxClothMeshQuadifier class.
	*/
    PxClothMeshDesc getDescriptor() const;

private:
	PxClothMeshQuadifierImpl* mImpl;

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif // PX_PHYSICS_EXTENSIONS_CLOTH_EDGE_QUADIFIER_H

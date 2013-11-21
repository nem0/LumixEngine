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


#ifndef PX_PHYSICS_NX_CLOTH_COLLISION_DATA
#define PX_PHYSICS_NX_CLOTH_COLLISION_DATA
/** \addtogroup cloth
  @{
*/

#include "PxPhysX.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
	\brief sphere representation used for cloth-capsule collision

	Cloth can collide with capsules.  Each capsule is represented by \n
	a pair of spheres with possibly different radi.

	\see PxClothCollisionData
*/
struct PxClothCollisionSphere
{
	PxVec3 pos; //!< position of the sphere
	PxReal radius; //!< radius of the sphere.
};

/**
	\brief plane representation used for cloth-convex collision

	Cloth can collide with convexes.  Each convex is represented by \n
	a mask of the planes that make up the convex.

	\see PxClothCollisionData
*/
struct PxClothCollisionPlane
{
	PxVec3 normal; //!< The normal to the plane
	PxReal distance; //!< The distance from the origin
};

/**
	\brief Collision data used for cloth-sphere and cloth-capsule collision

	This structure is used to define radius and position of all the collision spheres.
	Furthermore, it is possible to build collision capsules between the specified spheres by
	providing index pairs pointing into the sphere data array

    \note one can reuse the same sphere to create multiple capsules sharing the sphere. \n
	      However, duplicating the same capsules will hurt the performance as well as the stability of the solver.
*/
class PxClothCollisionData
{
public:

	PxU32									numSpheres;  //!< total number of spheres, no more than 32
	const PxClothCollisionSphere*			spheres; //!< sphere data array
	PxU32									numPairs; //!< number of capsules, no more than 32
	const PxU32*							pairIndexBuffer; //!< capsule indices (into the sphere data array)

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxClothCollisionData();
	/**
	\brief (re)sets the structure to the default.	
	*/
	PX_INLINE void setToDefault();
	/**
	\brief Returns true if the descriptor is valid.

	\return True if the current settings are valid
	*/
	PX_INLINE bool isValid() const;
};

PX_INLINE PxClothCollisionData::PxClothCollisionData()
{
	numSpheres = 0;
	spheres = NULL;
	numPairs = 0;
	pairIndexBuffer = NULL;
}

PX_INLINE void PxClothCollisionData::setToDefault()
{
	*this = PxClothCollisionData();
}

PX_INLINE bool PxClothCollisionData::isValid() const
{
	if((numSpheres && !spheres) || numSpheres > 32) 	// missing info for specified number of spheres
		return false;
	if(numPairs && !numSpheres)  // can only specify pairs, if there are spheres
		return false;
	if ((numPairs && !pairIndexBuffer) || numPairs > 32)  // missing index info for specified number of pairs
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_EXTENSIONS_BROAD_PHASE_H
#define PX_PHYSICS_EXTENSIONS_BROAD_PHASE_H
/** \addtogroup extensions
  @{
*/

#include "PxPhysXConfig.h"
#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxBroadPhaseExt
{
public:

	/**
	\brief Creates regions for PxSceneDesc, from a global box.

	This helper simply subdivides the given global box into a 2D grid of smaller boxes. Each one of those smaller boxes
	is a region of interest for the broadphase. There are nbSubdiv*nbSubdiv regions in the 2D grid. The function does not
	subdivide along the given up axis.

	This is the simplest setup one can use with PxBroadPhaseType::eMBP. A more sophisticated setup would try to cover
	the game world with a non-uniform set of regions (i.e. not just a grid).

	\param[out]	regions			Regions computed from the input global box
	\param[in]	globalBounds	World-space box covering the game world
	\param[in]	nbSubdiv		Grid subdivision level. The function will create nbSubdiv*nbSubdiv regions.
	\param[in]	upAxis			Up axis (0 for X, 1 for Y, 2 for Z).
	\return		number of regions written out to the 'regions' array

	@see PxSceneDesc PxBroadPhaseType
	*/
	static	PxU32	createRegionsFromWorldBounds(PxBounds3* regions, const PxBounds3& globalBounds, PxU32 nbSubdiv, PxU32 upAxis=1);
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

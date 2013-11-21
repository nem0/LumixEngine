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


#ifndef PX_PHYSICS_NXMATERIAL_FLAGS
#define PX_PHYSICS_NXMATERIAL_FLAGS
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Flags which control the behavior of a material.

@see PxMaterial 
*/
struct PxMaterialFlag
{
	enum Enum
	{

		/**
		If this flag is set, friction computations are always skipped between shapes with this material and any other shape.
		*/
		eDISABLE_FRICTION = 1 << 0,

		/**
		The difference between "normal" and "strong" friction is that the strong friction feature
		remembers the "friction error" between simulation steps. The friction is a force trying to
		hold objects in place (or slow them down) and this is handled in the solver. But since the
		solver is only an approximation, the result of the friction calculation can include a small
		"error" - e.g. a box resting on a slope should not move at all if the static friction is in
		action, but could slowly glide down the slope because of a small friction error in each 
		simulation step. The strong friction counter-acts this by remembering the small error and
		taking it to account during the next simulation step.

		However, in some cases the strong friction could cause problems, and this is why it is
		possible to disable the strong friction feature by setting this flag. One example is
		raycast vehicles, that are sliding fast across the surface, but still need a precise
		steering behavior. It may be a good idea to reenable the strong friction when objects
		are coming to a rest, to prevent them from slowly creeping down inclines.

		Note: This flag only has an effect if the PxMaterialFlag::eDISABLE_FRICTION bit is 0.
		*/
		eDISABLE_STRONG_FRICTION = 1 << 1,
	};
};

/**
\brief collection of set bits defined in PxMaterialFlag.

@see PxMaterialFlag
*/
typedef PxFlags<PxMaterialFlag::Enum,PxU16> PxMaterialFlags;
PX_FLAGS_OPERATORS(PxMaterialFlag::Enum,PxU16);


/**
\brief enumeration that determines the way in which two material properties will be combined to yield a friction or restitution coefficient for a collision.

When two actors come in contact with each other, they each have materials with various coefficients, but we only need a single set of coefficients for the pair.

Physics doesn't have any inherent combinations because the coefficients are determined empirically on a case by case
basis. However, simulating this with a pairwise lookup table is often impractical.

For this reason the following combine behaviors are available:

eAVERAGE
eMIN
eMULTIPLY
eMAX

The effective combine mode for the pair is maximum(material0.combineMode, material1.combineMode).

@see PxMaterial.setFrictionCombineMode() PxMaterial.getFrictionCombineMode() PxMaterial.setRestitutionCombineMode() PxMaterial.getFrictionCombineMode()
*/
struct PxCombineMode
{
	enum Enum
	{
		eAVERAGE	= 0,		//!< Average: (a + b)/2
		eMIN		= 1,		//!< Minimum: minimum(a,b)
		eMULTIPLY	= 2,		//!< Multiply: a*b
		eMAX		= 3,		//!< Maximum: maximum(a,b)
		eN_VALUES	= 4,		//!< This is not a valid combine mode, it is a sentinel to denote the number of possible values. We assert that the variable's value is smaller than this.
		ePAD_32		= 0x7fffffff //!< This is not a valid combine mode, it is to assure that the size of the enum type is big enough.
	};
};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */

#endif
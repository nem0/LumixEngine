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


#ifndef PX_SCALE_H
#define PX_SCALE_H

/** \addtogroup common
  @{
*/

#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPhysics;

/**
\brief Class to define the scale at which simulation runs. Most simulation tolerances are
calculated in terms of the values here. 

\note if you change the simulation scale, you will probablly also wish to change the scene's
default value of gravity, and stable simulation will probably require changes to the scene's 
bounceThreshold also.
*/

class PxTolerancesScale
{
public: 

	/** brief
	The approximate size of objects in the simulation. 
	
	For simulating roughly human-sized in metric units, 1 is a good choice.
	If simulation is done in centimetres, use 100 instead. This is used to
	estimate certain length-related tolerances.

	*/

	PxReal	length;


	/** brief
	The approximate mass of a length * length * length block.
	If using metric scale for character sized objects and measuring mass in
	kilogrammes, 1000 is a good choice.	
	*/
	PxReal	mass;

	/** brief
	The typical magnitude of velocities of objects in simulation. This is used to estimate 
	whether a contact should be treated as bouncing or resting based on its impact velocity,
	and a kinetic energy threshold below which the simulation may put objects to sleep.

	For normal physical environments, a good choice is the approximate speed of an object falling
	under gravity for one second.
	*/
	PxReal	speed;


	/**
	\brief constructor sets to default 
	*/
	PX_INLINE PxTolerancesScale();

	/**
	\brief Returns true if the descriptor is valid.
	\return true if the current settings are valid (returns always true).
	*/
	PX_INLINE bool isValid() const;

};

PX_INLINE PxTolerancesScale::PxTolerancesScale():
	length(1),
	mass(1000),
	speed(10)
	{
	}

PX_INLINE bool PxTolerancesScale::isValid() const
{
	return length>0 && mass>0;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

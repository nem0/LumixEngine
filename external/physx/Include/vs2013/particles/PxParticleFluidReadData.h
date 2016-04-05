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


#ifndef PX_PARTICLESYSTEM_NXPARTICLEFLUIDREADDATA
#define PX_PARTICLESYSTEM_NXPARTICLEFLUIDREADDATA
/** \addtogroup particles
  @{
*/

#include "particles/PxParticleReadData.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Data layout descriptor for reading fluid particle data from the SDK.

Additionally to PxParticleReadData, the density can be read from the SDK.
@see PxParticleReadData PxParticleFluid.lockParticleFluidReadData()
*/
class PxParticleFluidReadData : public PxParticleReadData
	{
	public:

	/**
	\brief Particle density data.
	
	The density depends on how close particles are to each other. The density values are normalized such that:
	<ol>
	<li>
	Particles which have no neighbors (no particles closer than restParticleDistance * 2) 
	will have a density of zero.
	<li>
	Particles which are at rest density (distances corresponding to restParticleDistance in the mean) 
	will have a density of one.
	</ol>
	 
	The density buffer is only guaranteed to be valid after the particle 
	fluid has been simulated. Otherwise densityBuffer.ptr() is NULL. This also 
	applies to particle fluids that are not assigned to a scene.
	*/
	PxStrideIterator<const PxF32> densityBuffer;

	/**
	\brief virtual destructor
	*/
	virtual ~PxParticleFluidReadData() {}

	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

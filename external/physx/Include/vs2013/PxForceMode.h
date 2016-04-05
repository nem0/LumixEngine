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


#ifndef PX_PHYSICS_NX_FORCE_MODE
#define PX_PHYSICS_NX_FORCE_MODE

/** \addtogroup physics
@{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Parameter to addForce() and addTorque() calls, determines the exact operation that is carried out.

@see PxRigidBody.addForce() PxRigidBody.addTorque()
*/
struct PxForceMode
{
	enum Enum
	{
		eFORCE,				//!< parameter has unit of mass * distance/ time^2, i.e. a force
		eIMPULSE,			//!< parameter has unit of mass * distance /time
		eVELOCITY_CHANGE,	//!< parameter has unit of distance / time, i.e. the effect is mass independent: a velocity change.
		eACCELERATION		//!< parameter has unit of distance/ time^2, i.e. an acceleration. It gets treated just like a force except the mass is not divided out before integration.
	};
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

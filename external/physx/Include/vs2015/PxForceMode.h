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

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


#ifndef PX_PHYSICS_PX_PHYSICS_INSERTION_CALLBACK
#define PX_PHYSICS_PX_PHYSICS_INSERTION_CALLBACK

#include "PxBase.h"

/** \addtogroup common
@{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	/**

	\brief PxPhysicsInsertionCallback does provide interface from PxCooking to be able to insert
	TriangleMesh or HeightfieldMesh directly into PxPhysics without the need of storing
	the cooking results into stream. 

	This advised only if real-time cooking is required, using "offline" cooking and
	streams is highly advised.

	Only default PxPhysicsInsertionCallback implementation must be used. The PxPhysics
	default callback can be obtained using the PxPhysics::getPhysicsInsertionCallback().

	@see PxCooking PxPhysics
	*/
	class PxPhysicsInsertionCallback
	{
	public:
		PxPhysicsInsertionCallback()				{}		

		/**
		\brief Inserts object (TriangleMesh or HeightfieldMesh) into PxPhysics.		

		\param obj Object to insert.
		*/
		virtual bool insertObject(PxBase& obj)			= 0;

	protected:
		virtual ~PxPhysicsInsertionCallback()		{}
	};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

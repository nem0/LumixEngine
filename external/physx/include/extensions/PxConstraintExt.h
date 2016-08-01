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


#ifndef PX_PHYSICS_EXTENSIONS_CONSTRAINT_H
#define PX_PHYSICS_EXTENSIONS_CONSTRAINT_H
/** \addtogroup extensions
  @{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Unique identifiers for extensions classes which implement a constraint based on PxConstraint.

\note Users which want to create their own custom constraint types should choose an ID larger or equal to eNEXT_FREE_ID
and not eINVALID_ID.

@see PxConstraint PxSimulationEventCallback.onConstraintBreak()
*/
struct PxConstraintExtIDs
{
	enum Enum
	{
		eJOINT,
		eVEHICLE_SUSP_LIMIT,
		eVEHICLE_STICKY_TYRE,
		eNEXT_FREE_ID,
		eINVALID_ID = 0x7fffffff
	};
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_NX_LOCKED_DATA
#define PX_PHYSICS_NX_LOCKED_DATA
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

struct PxDataAccessFlag
{
	enum Enum
	{
		eREADABLE = (1 << 0),
		eWRITABLE = (1 << 1),
		eDEVICE	  = (1 << 2)
	};
};

/**
\brief collection of set bits defined in PxDataAccessFlag.

@see PxDataAccessFlag
*/
typedef PxFlags<PxDataAccessFlag::Enum,PxU8> PxDataAccessFlags;
PX_FLAGS_OPERATORS(PxDataAccessFlag::Enum,PxU8)


/**
\brief Parent class for bulk data that is shared between the SDK and the application.
*/
class PxLockedData
{ 
public:

	/**
	\brief Any combination of PxDataAccessFlag::eREADABLE and PxDataAccessFlag::eWRITABLE
	@see PxDataAccessFlag
	*/
    virtual PxDataAccessFlags getDataAccessFlags() = 0;

	/**
	\brief Unlocks the bulk data.
	*/
    virtual void unlock() = 0;

	/**
	\brief virtual destructor
	*/
	virtual ~PxLockedData() {}
}; 

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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

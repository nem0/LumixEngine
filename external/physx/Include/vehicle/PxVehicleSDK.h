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

#ifndef PX_VEHICLE_SDK_H
#define PX_VEHICLE_SDK_H
/** \addtogroup vehicle
  @{
*/

#include "foundation/Px.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPhysics;

/**
\brief Call this before using any of the vehicle functions.

@see PxCloseVehicleSDK
*/
PX_C_EXPORT bool PX_CALL_CONV PxInitVehicleSDK(PxPhysics& physics);

/**
\brief Shut down the PhysXVehicle library. 

This function should be called to cleanly shut down the PhysXVehicle library before the PxPhysics SDK gets released.

@see PxInitVehicleSDK
*/
PX_C_EXPORT void PX_CALL_CONV PxCloseVehicleSDK();

/**
\brief Maximum number of wheel shapes allowed to be added to the actor.

This number is also the maximum number of wheels allowed for a vehicle.
*/
#define PX_MAX_NUM_WHEELS (20)


/**
\brief Compiler setting to enable recording of telemetry data

@see PxVehicleUpdateSingleVehicleAndStoreTelemetryData, PxVehicleTelemetryData
*/
#define PX_DEBUG_VEHICLE_ON (1)

/**
@see PxVehicleDrive4W, PxVehicleDriveTank, PxVehicleWheels::getVehicleType
*/
enum eVehicleDriveTypes
{
	eVEHICLE_TYPE_DRIVE4W=0,
	eVEHICLE_TYPE_DRIVETANK,
	eVEHICLE_TYPE_USER0,
	eVEHICLE_TYPE_USER1,
	eVEHICLE_TYPE_USER2,
	eVEHICLE_TYPE_USER3,
	eMAX_NUM_VEHICLE_TYPES
};

/**
\brief Set the basis vectors of the vehicle simulation 

Default values PxVec3(0,1,0), PxVec3(0,0,1)

Call this function before using PxVehicleUpdates unless the default values are correct.
*/
void PxVehicleSetBasisVectors(const PxVec3& up, const PxVec3& forward);


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_SDK_H

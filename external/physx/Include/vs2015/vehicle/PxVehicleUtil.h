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

#ifndef PX_VEHICLE_UTILHELPER_H
#define PX_VEHICLE_UTILHELPER_H
/** \addtogroup vehicle
  @{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVehicleWheels;
struct PxVehicleWheelQueryResult;

/**
\brief Test if all wheels of a vehicle are in the air by querying the wheel query data 
stored in the last call to PxVehicleUpdates. If all wheels are in the air then true is returned.  

\note False is returned if any wheel can reach to the ground.

\note If vehWheelQueryResults.wheelQueryResults is NULL or vehWheelQueryResults.nbWheelQueryResults is 0 then true is returned.
This function does not account for wheels that have been disabled since the last execution of PxVehicleUpdates so it is possible
that wheels disabled more recently than the last call to PxVehicleUpdates report are treated as touching the ground.

\return True if the vehicle is in the air, false if any wheel is touching the ground.
*/
bool PxVehicleIsInAir(const PxVehicleWheelQueryResult& vehWheelQueryResults);


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_UTILHELPER_H

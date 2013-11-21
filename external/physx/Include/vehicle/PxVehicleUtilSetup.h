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

#ifndef PX_VEHICLE_UTILSSETUP_H
#define PX_VEHICLE_UTILSSETUP_H
/** \addtogroup vehicle
  @{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVehicleWheelsSimData;
class PxVehicleDriveSimData4W;


/**
\brief Enable 4W vehicle sim data as a 3W car with tadpole config (2 front wheels, 1 rear wheel)
\brief The rear-left wheel is removed and the rear-right wheel is positioned at the centre of the rear axle.
\brief The suspension of the rear-right wheel is modified to support the entire mass of the front car while preserving its natural frequency and damping ratio.
*/
void PxVehicle4WEnable3WTadpoleMode(PxVehicleWheelsSimData& suspWheelTireData, PxVehicleDriveSimData4W& coreData);

/**
\brief Enable as a vehicle with 3 driven wheels with delta config (1 front wheel, 2 rear wheels)
\brief The front-left wheel is removed and the front-right wheel is positioned at the centre of the front axle.
\brief The suspension of the front-right wheel is modified to support the entire mass of the front car while preserving its natural frequency and damping ratio.
*/
void PxVehicle4WEnable3WDeltaMode(PxVehicleWheelsSimData& suspWheelTireData, PxVehicleDriveSimData4W& coreData);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_UTILSSETUP_H

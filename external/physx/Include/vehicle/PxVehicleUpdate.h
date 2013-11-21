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

#ifndef PX_VEHICLE_UPDATE_H
#define PX_VEHICLE_UPDATE_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleSDK.h"
#include "foundation/PxSimpleTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxBatchQuery;
	struct PxRaycastQueryResult;
	class PxVehicleWheels;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
	class PxVehicleTelemetryData;

	/**
	\brief Start raycasts of all suspension lines.
	\brief numSceneQueryResults specifies the size of the sceneQueryResults array. 
	\brief numVehicles is the number of vehicles in the vehicles array.
	\brief numSceneQueryResults must be greater than or equal to the total number of wheels of all the vehicles in the vehicles array; that is,
	\brief sceneQueryResults must have dimensions large enough for one raycast per wheel.
	*/
	void PxVehicleSuspensionRaycasts(PxBatchQuery* batchQuery, const PxU32 numVehicles, PxVehicleWheels** vehicles, const PxU32 numSceneQueryResults, PxRaycastQueryResult* sceneQueryResults);

	/**
	\brief Update an array of vehicles.
	*/
	void PxVehicleUpdates(const PxReal timestep, const PxVec3& gravity, const PxVehicleDrivableSurfaceToTireFrictionPairs& vehicleDrivableSurfaceToTireFrictionPairs, const PxU32 numVehicles, PxVehicleWheels** vehicles);

#if PX_DEBUG_VEHICLE_ON
	/**
	\brief Update the focus vehicle and also store key debug data for the specified focus vehicle.
	*/
	void PxVehicleUpdateSingleVehicleAndStoreTelemetryData
		(const PxReal timestep, const PxVec3& gravity, const PxVehicleDrivableSurfaceToTireFrictionPairs& vehicleDrivableSurfaceToTireFrictionPairs, 
		 PxVehicleWheels* focusVehicle, PxVehicleTelemetryData& telemetryData);
#endif

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_UPDATE_H

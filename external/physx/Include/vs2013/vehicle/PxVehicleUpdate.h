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

#ifndef PX_VEHICLE_UPDATE_H
#define PX_VEHICLE_UPDATE_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleSDK.h"
#include "vehicle/PxVehicleTireFriction.h"
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxMemory.h"
#include "foundation/PxTransform.h"
#include "PxBatchQueryDesc.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxBatchQuery;
	class PxVehicleWheels;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
	class PxVehicleTelemetryData;

	/**
	\brief Structure containing data describing the non-persistent state of each suspension/wheel/tire unit.
	This structure is filled out in PxVehicleUpdates and PxVehicleUpdateSingleVehicleAndStoreTelemetryData
	@see PxVehicleUpdates, PxVehicleUpdateSingleVehicleAndStoreTelemetryData
	*/
	struct PxWheelQueryResult
	{
		PxWheelQueryResult()
		{
			PxMemZero(this, sizeof(PxWheelQueryResult));
			isInAir=true;
			tireSurfaceType = (PxU32)PxVehicleDrivableSurfaceType::eSURFACE_TYPE_UNKNOWN;
			localPose = PxTransform(PxIdentity);
		}

		/**
		\brief Start point of suspension line raycast used in raycast completed immediately before PxVehicleUpdates.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then (0,0,0) is stored.
		@see PxVehicle4WSuspensionRaycasts
		*/
		PxVec3 suspLineStart;

		/**
		\brief Directions of suspension line raycast used in raycast completed immediately before PxVehicleUpdates.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then (0,0,0) is stored.
		@see PxVehicle4WSuspensionRaycasts
		*/
		PxVec3 suspLineDir;

		/**
		\brief Lengths of suspension line raycast used in raycast completed immediately before PxVehicleUpdates.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then 0 is stored.
		@see PxVehicle4WSuspensionRaycasts
		*/
		PxReal suspLineLength;

		/**
		\brief If suspension travel limits forbid the wheel from touching the drivable surface then isInAir is true.
		\note If the wheel can be placed on the contact plane of the most recent suspension line raycast then isInAir is false.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then isInAir
		is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/
		bool isInAir;

		/**
		\brief PxActor instance of the driving surface under the corresponding vehicle wheel.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireContactActor is NULL.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then NULL is stored.
		*/
		PxActor* tireContactActor;

		/**
		\brief PxShape instance of the driving surface under the corresponding vehicle wheel.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireContactShape is NULL.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then NULL is stored.
		*/
		PxShape* tireContactShape;

		/**
		\brief PxMaterial instance of the driving surface under the corresponding vehicle wheel.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireSurfaceMaterial is NULL.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then NULL is stored.
		*/	
		const PxMaterial* tireSurfaceMaterial;

		/**
		\brief Surface type integer that corresponds to the mapping between tireSurfaceMaterial and integer as
		described in PxVehicleDrivableSurfaceToTireFrictionPairs.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireSurfaceType is 
		PxVehicleDrivableSurfaceType::eSURFACE_TYPE_UNKNOWN.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then 
		PxVehicleDrivableSurfaceType::eSURFACE_TYPE_UNKNOWN is stored.
		@see PxVehicleDrivableSurfaceToTireFrictionPairs
		*/	
		PxU32 tireSurfaceType;

		/**
		\brief Point on the drivable surface hit by the most recent suspension raycast.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then the contact point is (0,0,0).
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then (0,0,0) is stored.
		*/
		PxVec3 tireContactPoint;

		/**
		\brief Normal on the drivable surface at the hit point of the most recent suspension raycast.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then the contact normal is (0,0,0).
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then (0,0,0) is stored.
		*/
		PxVec3 tireContactNormal;

		/**
		\brief Friction experienced by the tire for the combination of tire type and surface type after accounting 
		for the friction vs slip graph.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then the tire friction is 0.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		stored tire friction is the value computed in PxVehicleUpdates that immediately followed the last raycast.
		@see PxVehicleDrivableSurfaceToTireFrictionPairs, PxVehicleTireData
		*/	
		PxReal tireFriction;

		/**
		\brief Compression of the suspension spring.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then the jounce is -PxVehicleSuspensionData.mMaxDroop
		The jounce can never exceed PxVehicleSuspensionData.mMaxCompression. Positive values result when the suspension is compressed from 
		the rest position, while negative values mean the suspension is elongated from the rest position.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		suspension compression is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/	
		PxReal suspJounce;

		/**
		\brief Magnitude of force applied by the suspension spring along the direction of suspension travel.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then the force is 0
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		suspension spring force is computed using the contact plane that was hit by the most recent suspension line raycast.
		@see PxVehicleWheelsSimData::getSuspTravelDirection
		*/
		PxReal suspSpringForce;

		/**
		\brief Forward direction of the wheel/tire accounting for steer/toe/camber angle projected on to the contact plane of the drivable surface.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireLongitudinalDir is (0,0,0)
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		tire longitudinal direction is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/
		PxVec3 tireLongitudinalDir;

		/**
		\brief Lateral direction of the wheel/tire accounting for steer/toe/camber angle projected on to the contact plan of the drivable surface.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then tireLateralDir is (0,0,0)
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		tire lateral direction is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/
		PxVec3 tireLateralDir;

		/**
		\brief Longitudinal slip of the tire.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then longitudinalSlip is 0.0
		\note The longitudinal slip is approximately (w*r - vz) / PxAbs(vz) where w is the angular speed of the wheel, r is the radius of the wheel, and 
		vz component of rigid body velocity computed at the wheel base along the longitudinal direction of the tire.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		tire longitudinal slip is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/	
		PxReal longitudinalSlip;

		/**
		\brief Lateral slip of the tire.
		\note If suspension travel limits forbid the wheel from touching the drivable surface then lateralSlip is 0.0
		\note The lateral slip angle is approximately PxAtan(vx / PxAbs(vz)) where vx and vz are the components of rigid body velocity at the wheel base 
		along the wheel's lateral and longitudinal directions, respectively.
		\note If no raycast for the corresponding suspension was performed immediately prior to PxVehicleUpdates then the 
		tire lateral slip is computed using the contact plane that was hit by the most recent suspension line raycast.
		*/	
		PxReal lateralSlip;

		/**
		\brief Steer angle of the wheel about the "up" vector accounting for input steer and toe and, if applicable, Ackermann steer correction.
		@see PxVehicleWheelData::mToeAngle
		*/	
		PxReal steerAngle;

		/**
		\brief Local pose of the wheel.
		*/
		PxTransform localPose;
	};

	struct PxVehicleWheelQueryResult
	{
		/**
		\brief Pointer to an PxWheelQueryResult buffer of length nbWheelQueryResults
		The wheelQueryResults buffer must persist until the end of PxVehicleUpdates
		A NULL pointer is permitted.
		The wheelQueryResults buffer is left unmodified in PxVehicleUpdates for vehicles with sleeping rigid bodies 
		whose control inputs indicate they should remain inert.
		@see PxVehicleUpdates
		*/
		PxWheelQueryResult* wheelQueryResults;

		/**
		\brief The length of the wheelQueryResults buffer.  This value corresponds to the 
		number of wheels in the associated vehicle in PxVehicleUpdates.
		*/
		PxU32 nbWheelQueryResults;
	};

	/**
	\brief Structure containing data that is computed for a wheel during concurrent calls to PxVehicleUpdates
	but which cannot be safely concurrently applied. 

	@see PxVehicleUpdates, PxVehiclePostUpdates, PxVehicleConcurrentUpdate
	*/
	struct PxVehicleWheelConcurrentUpdateData
	{
		friend class PxVehicleUpdate;

		PxVehicleWheelConcurrentUpdateData()
			: localPose(PxTransform(PxIdentity)),
			  hitActor(NULL),
			  hitActorForce(PxVec3(0,0,0)),
			  hitActorForcePosition(PxVec3(0,0,0))
		{
		}

	private:

		PxTransform localPose;
		PxRigidDynamic* hitActor;
		PxVec3 hitActorForce;
		PxVec3 hitActorForcePosition;
	};

	/**
	\brief Structure containing data that is computed for a vehicle and its wheels during concurrent calls to PxVehicleUpdates
	but which cannot be safely concurrently applied. 

	@see PxVehicleUpdates, PxVehiclePostUpdates, PxVehicleWheelConcurrentUpdateData
	*/

	struct PxVehicleConcurrentUpdateData
	{
		friend class PxVehicleUpdate;

		PxVehicleConcurrentUpdateData()
			: concurrentWheelUpdates(NULL),
			  nbConcurrentWheelUpdates(0),
			  linearMomentumChange(PxVec3(0,0,0)),
			  angularMomentumChange(PxVec3(0,0,0)),
			  staySleeping(false),
			  wakeup(false)
		{
		}

		/**
		\brief Pointer to an PxVehicleWheelConcurrentUpdate buffer of length nbConcurrentWheelUpdates
		The concurrentWheelUpdates buffer must persist until the end of PxVehiclePostUpdates
		A NULL pointer is not permitted.
		@see PxVehicleUpdates, PxVehiclePostUpdates
		*/
		PxVehicleWheelConcurrentUpdateData* concurrentWheelUpdates;

		/**
		\brief The length of the concurrentWheelUpdates buffer.  This value corresponds to the 
		number of wheels in the associated vehicle passed to PxVehicleUpdates.
		*/
		PxU32 nbConcurrentWheelUpdates;

	private:

		PxVec3 linearMomentumChange;
		PxVec3 angularMomentumChange;
		bool staySleeping;
		bool wakeup;
	};

	/**
	\brief Perform raycasts for all suspension lines for all vehicles.

	\param[in] batchQuery is a PxBatchQuery instance used to specify shader data and functions for the raycast scene queries.

	\param[in] nbVehicles is the number of vehicles in the vehicles array.

	\param[in] vehicles is an array of all vehicles that are to have a raycast issued from each wheel. 

	\param[in] nbSceneQueryResults must be greater than or equal to the total number of wheels of all the vehicles in the vehicles array; that is,
	sceneQueryResults must have dimensions large enough for one raycast hit result per wheel for all the vehicles in the vehicles array.

	\param[in] sceneQueryResults must persist without being overwritten until the end of the next PxVehicleUpdates call. 

	\param[in] vehiclesToRaycast is an array of bools of length nbVehicles that is used to decide if raycasts will be performed for the corresponding vehicle
	in the vehicles array. If vehiclesToRaycast[i] is true then suspension line raycasts will be performed for  vehicles[i].  If vehiclesToRaycast[i] is 
	false then suspension line raycasts will not be performed for vehicles[i].  

	\note If vehiclesToRaycast is NULL then raycasts are performed for all vehicles in the vehicles array.

	\note If vehiclesToRaycast[i] is false then the vehicle stored in vehicles[i] will automatically use the raycast hit planes recorded by the most recent
	suspension line raycasts for that vehicle.  For vehicles far from the camera or not visible on the screen it can be 
	optimal to only perform suspension line raycasts every Nth update rather than every single update.  The accuracy of the cached contact plane
	naturally diminishes as N increase, meaning that wheels might start to hover or intersect the ground for large values of N or even with values close to 1 in
	conjunction with large vehicle speeds and/or geometry that has low spatial coherence.

	\note Calling setToRestState invalidates any cached hit planes. Prior to calling PxVehicleUpdates each vehicle needs to perform suspension line raycasts 
	at least once after instantiation and at least once after calling setToRestState.

	\note Each raycast casts along the suspension travel direction from the position of the top of the wheel at maximum suspension compression
	to the position of the base of the wheel at maximum droop.  Raycasts that start inside a PxShape are subsequently ignored by the
	corresponding vehicle.

	@see PxVehicleDrive4W::setToRestState, PxVehicleDriveNW::setToRestState, PxVehicleDriveTank::setToRestState, PxVehicleNoDrive::setToRestState
	*/
	void PxVehicleSuspensionRaycasts
		(PxBatchQuery* batchQuery, const PxU32 nbVehicles, PxVehicleWheels** vehicles, const PxU32 nbSceneQueryResults, PxRaycastQueryResult* sceneQueryResults, const bool* vehiclesToRaycast = NULL);


	/**
	\brief Update an array of vehicles by either applying an acceleration to the rigid body actor associated with 
	each vehicle or by an immediate update of the velocity of the actor.
	
	\note The update mode (acceleration or velocity change) can be selected with PxVehicleSetUpdateMode.

	\param[in] timestep is the timestep of the update

	\param[in] gravity is the value of gravitational acceleration

	\param[in] vehicleDrivableSurfaceToTireFrictionPairs describes the mapping between each PxMaterial ptr and an integer representing a 
	surface type. It also stores the friction value for each combination of surface and tire type.

	\param[in] nbVehicles is the number of vehicles pointers in the vehicles array

	\param[in, out] vehicles is an array of length nbVehicles containing all vehicles to be updated by the specified timestep

	\param[out] vehicleWheelQueryResults is an array of length nbVehicles storing the wheel query results of each corresponding vehicle and wheel in the 
	vehicles array.  A NULL pointer is permitted.  

	\param[out] vehicleConcurrentUpdates is an array of length nbVehicles.  It is only necessary to specify vehicleConcurrentUpdates if PxVehicleUpdates is 
	called concurrently.  The element vehicleWheelQueryResults[i] of the array stores data that is computed for vehicle[i] during PxVehicleUpdates but which 
	cannot be safely written when concurrently called.  The data computed and stored in vehicleConcurrentUpdates must be passed to PxVehiclePostUpdates, where 
	it is applied to all relevant actors in sequence.  A NULL pointer is permitted.  
	
	\note The vehicleWheelQueryResults buffer must persist until the end of PxVehicleUpdates.
	
	\note The vehicleWheelQueryResults buffer is left unmodified for vehicles with sleeping rigid bodies whose control inputs indicate they should remain inert.

	\note If PxVehicleUpdates is called concurrently then vehicleConcurrentUpdates must be specified.  Do not specify vehicleConcurrentUpdates is PxVehicleUpdates
	is not called concurrently.

	\note The vehicleConcurrentUpdates buffer must persist until the end of PxVehiclePostUpdate.

	\note If any vehicle has one or more disabled wheels (PxVehicleWheelsSimData::disableWheel) then the disabled wheels must not be associated 
	with a PxShape (PxVehicleWheelsSimData::setWheelShapeMapping); the differential of the vehicle must be configured so that no drive torque 
	is delivered to a disabled wheel; and the wheel must have zero rotation speed (PxVehicleWheelsDynData::setWheelRotationSpeed)

	\note PxVehicleUpdates may be called concurrently provided all concurrent calls to PxVehicleUpdates involve only vehicles in the scene specified by PxVehicleUpdateSetScene.  
	PxVehicleUpdates must never run concurrently with PxVehicleUpdateSingleVehicleAndStoreTelemetryData.

	@see PxVehicleSetUpdateMode, PxVehicleWheelsSimData::disableWheel, PxVehicleWheelsSimData::setWheelShapeMapping, PxVehicleWheelsDynData::setWheelRotationSpeed,
	PxVehiclePostUpdates
	*/
	void PxVehicleUpdates(
		const PxReal timestep, const PxVec3& gravity, 
		const PxVehicleDrivableSurfaceToTireFrictionPairs& vehicleDrivableSurfaceToTireFrictionPairs, 
		const PxU32 nbVehicles, PxVehicleWheels** vehicles, PxVehicleWheelQueryResult* vehicleWheelQueryResults, PxVehicleConcurrentUpdateData* vehicleConcurrentUpdates = NULL);


	/**
	\brief Apply actor changes that were computed in concurrent calls to PxVehicleUpdates but which could not be safely applied due to the concurrency.

	\param[in] vehicleConcurrentUpdates is an array of length nbVehicles where vehicleConcurrentUpdates[i] contains data describing actor changes that 
	were computed for vehicles[i] during concurrent calls to PxVehicleUpdates.

	\param[in] nbVehicles is the number of vehicles pointers in the vehicles array

	\param[in, out] vehicles is an array of length nbVehicles containing all vehicles that were partially updated in concurrent calls to PxVehicleUpdates.

	@see PxVehicleUpdates
	*/
	void PxVehiclePostUpdates(
		const PxVehicleConcurrentUpdateData* vehicleConcurrentUpdates, const PxU32 nbVehicles, PxVehicleWheels** vehicles);


	/**
	\brief Shift the origin of vehicles by the specified vector.

	Call this method to adjust the internal data structures of vehicles to reflect the shifted origin location
	(the shift vector will get subtracted from all world space spatial data).

	\note It is the user's responsibility to keep track of the summed total origin shift and adjust all input/output to/from PhysXVehicle accordingly.

	\note This call will not automatically shift the PhysX scene and its objects. You need to call PxScene::shiftOrigin() seperately to keep the systems in sync.

	\param[in] shift is the translation vector to shift the origin by.

	\param[in] nbVehicles is the number of vehicles in the vehicles array.

	\param[in, out] vehicles is an array of all vehicles that should be updated to map to the new scene origin.
	*/
	void PxVehicleShiftOrigin(const PxVec3& shift, const PxU32 nbVehicles, PxVehicleWheels** vehicles);

#if PX_DEBUG_VEHICLE_ON
	/**
	\brief Update an single vehicle by either applying an acceleration to the rigid body actor associated with 
	each vehicle or by an immediate update of the velocity of the actor. Also record telemetry data from the 
	vehicle so that it may be visualized or queried.
	
	\note The update mode (acceleration or velocity change) can be selected with PxVehicleSetUpdateMode.

	\param[in] timestep is the timestep of the update

	\param[in] gravity is the value of gravitational acceleration

	\param[in] vehicleDrivableSurfaceToTireFrictionPairs describes the mapping between each PxMaterial ptr and an integer representing a 
	surface type. It also stores the friction value for each combination of surface and tire type.

	\param[in, out] focusVehicle is the vehicle to be updated and have its telemetry data recorded

	\param[out] vehicleWheelQueryResults is an array of length 1 storing the wheel query results of each wheel of the vehicle/ 
	A NULL pointer is permitted.  

	\param[out] telemetryData is the data structure used to record telemetry data during the update for later query or visualization

	\note The vehicleWheelQueryResults buffer must persist until the end of PxVehicleUpdates

	\note The vehicleWheelQueryResults buffer is left unmodified for vehicles with sleeping rigid bodies whose control inputs indicate they should remain inert.

	\note PxVehicleUpdateSingleVehicleAndStoreTelemetryData is not thread-safe.  As a consequence, it must run sequentially and never concurrently with PxVehicleUpdates

	@see PxVehicleSetUpdateMode, PxVehicleTelemetryData
	*/
	void PxVehicleUpdateSingleVehicleAndStoreTelemetryData
		(const PxReal timestep, const PxVec3& gravity, 
		 const PxVehicleDrivableSurfaceToTireFrictionPairs& vehicleDrivableSurfaceToTireFrictionPairs, 
		 PxVehicleWheels* focusVehicle, PxVehicleWheelQueryResult* vehicleWheelQueryResults, 
		 PxVehicleTelemetryData& telemetryData);
#endif

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_UPDATE_H

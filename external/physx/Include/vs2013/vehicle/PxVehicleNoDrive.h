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

#ifndef PX_VEHICLE_NO_DRIVE_H
#define PX_VEHICLE_NO_DRIVE_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleWheels.h"
#include "vehicle/PxVehicleComponents.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

struct PxFilterData;
class PxGeometry;
class PxPhysics;
class PxBatchQuery;
class PxVehicleDrivableSurfaceToTireFrictionPairs;
class PxShape;
class PxMaterial;
class PxRigidDynamic;

/**
\brief Data structure with instanced dynamics data and configuration data of a vehicle with no drive model.
*/
class PxVehicleNoDrive : public PxVehicleWheels
{
//= ATTENTION! =====================================================================================
// Changing the data layout of this class breaks the binary serialization format.  See comments for 
// PX_BINARY_SERIAL_VERSION.  If a modification is required, please adjust the getBinaryMetaData 
// function.  If the modification is made on a custom branch, please change PX_BINARY_SERIAL_VERSION
// accordingly.
//==================================================================================================
public:

	friend class PxVehicleUpdate;

	/**
	\brief Allocate a PxVehicleNoDrive instance for a vehicle without drive model and with nbWheels

	\param[in] nbWheels is the number of wheels on the vehicle.

	\return The instantiated vehicle.

	@see free, setup
	*/
	static PxVehicleNoDrive* allocate(const PxU32 nbWheels);

	/**
	\brief Deallocate a PxVehicleNoDrive instance.
	@see allocate
	*/
	void free();

	/**
	\brief Set up a vehicle using simulation data for the wheels.
	\param[in] physics is a PxPhysics instance that is needed to create special vehicle constraints that are maintained by the vehicle.
	\param[in] vehActor is a PxRigidDynamic instance that is used to represent the vehicle in the PhysX SDK.
	\param[in] wheelsData describes the configuration of all suspension/tires/wheels of the vehicle. The vehicle instance takes a copy of this data.
	\note It is assumed that the first shapes of the actor are the wheel shapes, followed by the chassis shapes.  To break this assumption use PxVehicleWheels::setWheelShapeMapping.
	@see allocate, free, setToRestState, PxVehicleWheels::setWheelShapeMapping
	*/
	void setup
		(PxPhysics* physics, PxRigidDynamic* vehActor, const PxVehicleWheelsSimData& wheelsData);

	/**
	\brief Allocate and set up a vehicle using simulation data for the wheels.
	\param[in] physics is a PxPhysics instance that is needed to create special vehicle constraints that are maintained by the vehicle.
	\param[in] vehActor is a PxRigidDynamic instance that is used to represent the vehicle in the PhysX SDK.
	\param[in] wheelsData describes the configuration of all suspension/tires/wheels of the vehicle. The vehicle instance takes a copy of this data.
	\note It is assumed that the first shapes of the actor are the wheel shapes, followed by the chassis shapes.  To break this assumption use PxVehicleWheels::setWheelShapeMapping.
	\return The instantiated vehicle.
	@see allocate, free, setToRestState, PxVehicleWheels::setWheelShapeMapping
	*/
	static PxVehicleNoDrive* create
		(PxPhysics* physics, PxRigidDynamic* vehActor, const PxVehicleWheelsSimData& wheelsData);

	/**
	\brief Set a vehicle to its rest state.  Aside from the rigid body transform, this will set the vehicle and rigid body 
	to the state they were in immediately after setup or create.
	\note Calling setToRestState invalidates the cached raycast hit planes under each wheel meaning that suspension line
	raycasts need to be performed at least once with PxVehicleSuspensionRaycasts before calling PxVehicleUpdates. 
	@see setup, create, PxVehicleSuspensionRaycasts, PxVehicleUpdates
	*/
	void setToRestState();

	/**
	\brief Set the brake torque to be applied to a specific wheel
	
	\note The applied brakeTorque persists until the next call to setBrakeTorque
	
	\note The brake torque is specified in Newton metres.

	\param[in] id is the wheel being given the brake torque
	\param[in] brakeTorque is the value of the brake torque
	*/
	void setBrakeTorque(const PxU32 id, const PxReal brakeTorque);

	/**
	\brief Set the drive torque to be applied to a specific wheel

	\note The applied driveTorque persists until the next call to setDriveTorque

	\note The brake torque is specified in Newton metres.

	\param[in] id is the wheel being given the brake torque
	\param[in] driveTorque is the value of the brake torque
	*/
	void setDriveTorque(const PxU32 id, const PxReal driveTorque);

	/**
	\brief Set the steer angle to be applied to a specific wheel
	
	\note The applied steerAngle persists until the next call to setSteerAngle
	
	\note The steer angle is specified in radians.

	\param[in] id is the wheel being given the steer angle
	\param[in] steerAngle is the value of the steer angle in radians.
	*/
	void setSteerAngle(const PxU32 id, const PxReal steerAngle);

	/**
	\brief Get the brake torque that has been applied to a specific wheel
	\param[in] id is the wheel being queried for its brake torque
	\return The brake torque applied to the queried wheel.
	*/
	PxReal getBrakeTorque(const PxU32 id) const;

	/**
	\brief Get the drive torque that has been applied to a specific wheel
	\param[in] id is the wheel being queried for its drive torque 
	\return The drive torque applied to the queried wheel.
	*/
	PxReal getDriveTorque(const PxU32 id) const;

	/**
	\brief Get the steer angle that has been applied to a specific wheel
	\param[in] id is the wheel being queried for its steer angle
	\return The steer angle (in radians) applied to the queried wheel.
	*/
	PxReal getSteerAngle(const PxU32 id) const;
		
private:

	PxReal* mSteerAngles;
	PxReal* mDriveTorques;
	PxReal* mBrakeTorques;

#ifdef PX_X64
	PxU32 mPad[2];
#else 
	PxU32 mPad[1];
#endif

	/**
	\brief Test if the instanced dynamics and configuration data has legal values.
	*/
	bool isValid() const;


//serialization
public:
									PxVehicleNoDrive(PxBaseFlags baseFlags) : PxVehicleWheels(baseFlags) {}	
	virtual		void				exportExtraData(PxSerializationContext&);
				void				importExtraData(PxDeserializationContext&);
	static		PxVehicleNoDrive*	createObject(PxU8*& address, PxDeserializationContext& context);
	static		void				getBinaryMetaData(PxOutputStream& stream);
	virtual		const char*			getConcreteTypeName() const			{ return "PxVehicleNoDrive";	}
	virtual		bool				isKindOf(const char* name)	const	{ return !strcmp("PxVehicleNoDrive", name) || PxBase::isKindOf(name); }
				PxU32				getNbSteerAngle() const { return mWheelsSimData.getNbWheels();	}
				PxU32				getNbDriveTorque() const	{ return mWheelsSimData.getNbWheels();	}
				PxU32				getNbBrakeTorque() const	{ return mWheelsSimData.getNbWheels();	}
protected:
									PxVehicleNoDrive();
									~PxVehicleNoDrive() {}
//~serialization
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleNoDrive) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_NO_DRIVE_H

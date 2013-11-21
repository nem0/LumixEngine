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

#ifndef PX_VEHICLE_DRIVE_TANK_H
#define PX_VEHICLE_DRIVE_TANK_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleDrive.h"
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
struct PxRaycastQueryResult;
class PxVehicleDrivableSurfaceToTireFrictionPairs;
class PxShape;
class PxMaterial;
class PxRigidDynamic;


/**
\brief Data structure with instanced dynamics data and configuration data of a tank.
*/
class PxVehicleDriveTank : public PxVehicleDrive
{
public:

	friend class PxVehicleUpdate;

	/**
	\brief The ordering of the driven wheels
	*/
	enum eWheelOrdering
	{
		eTANK_WHEEL_FRONT_LEFT=0,
		eTANK_WHEEL_FRONT_RIGHT,
		eTANK_WHEEL_1ST_FROM_FRONT_LEFT,
		eTANK_WHEEL_1ST_FROM_FRONT_RIGHT,
		eTANK_WHEEL_2ND_FROM_FRONT_LEFT,
		eTANK_WHEEL_2ND_FROM_FRONT_RIGHT,
		eTANK_WHEEL_3RD_FROM_FRONT_LEFT,
		eTANK_WHEEL_3RD_FROM_FRONT_RIGHT,
		eTANK_WHEEL_4TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_4TH_FROM_FRONT_RIGHT,
		eTANK_WHEEL_5TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_5TH_FROM_FRONT_RIGHT,
		eTANK_WHEEL_6TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_6TH_FROM_FRONT_RIGHT,
		eTANK_WHEEL_7TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_7TH_FROM_FRONT_RIGHT,
		eTANK_WHEEL_8TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_8TH_FROM_FRONT_RIGHT,
		eTANK_WHEEL_9TH_FROM_FRONT_LEFT,
		eTANK_WHEEL_9TH_FROM_FRONT_RIGHT
	};

	/**
	@see PxVehicleDrive::setAnalogInput, PxVehicleDrive::getAnalogInput
	*/
	enum
	{
		eANALOG_INPUT_ACCEL=PxVehicleDriveDynData::eANALOG_INPUT_ACCEL,		
		eANALOG_INPUT_BRAKE_LEFT,	
		eANALOG_INPUT_BRAKE_RIGHT,	
		eANALOG_INPUT_THRUST_LEFT,	
		eANALOG_INPUT_THRUST_RIGHT,	
		eMAX_NUM_DRIVETANK_ANALOG_INPUTS
	};

	/**
	\brief Two driving models supported
	@see setDrivingModel
	*/
	enum eDriveModel
	{
		eDRIVE_MODEL_STANDARD=0,
		eDRIVE_MODEL_SPECIAL
	};

	/**
	\brief Allocate a PxVehicleTankDrive instance for a tank with numWheels
	\brief It is assumed that all wheels are driven wheels.
	@see free, setup
	*/
	static PxVehicleDriveTank* allocate(const PxU32 numWheels);

	/**
	\brief Deallocate a PxVehicleDriveTank instance.
	@see allocate
	*/
	void free();

	/**
	\brief Set up a tank with 
	\brief (i)	a PxPhysics instance - needed to setup special vehicle constraints maintained by the vehicle.
	\brief (ii)	a PxRigidDynamic instance - the rigid body representation of the tank in the physx sdk.
	\brief (iii)a PxVehicleWheelsSimData instance describing the wheel/suspension/tires - the tank instance takes a copy of this input data.
	\brief (iv)	a PxVehicleDriveSimData instance describing the drive data - the tank instance takes a copy of this input data.
	\brief (v)	the number of driven wheels.
	\brief (vi)	it is assumed that the first numDrivenWheel shapes of vehActor correspond to the wheel shapes of the tank
	\brief (vii)it is assumed that the even-numbered wheel shapes are the left-hand wheels ordered in sequence from front to back
	\brief (viii)it is assumed that the odd-numbered wheel shapes are the right-hand wheels ordered in sequence from front to back 
	\brief (ix)	it is assumed that the N non-driven wheel shapes are shapes 4 -> 4+N-1 of the actor.
	\brief (x)	it is assumed that the ordering of the wheel shapes in the actor matches the ordering in PxVehicleWheelsSimData.
	\brief (xi)	to break assumpgions (vi)-(x) see PxVehicleWheels::setWheelShapeMapping
	@see allocate, free, setToRestState, eWheelOrdering
	*/
	void setup
		(PxPhysics* physics, PxRigidDynamic* vehActor, 
		 const PxVehicleWheelsSimData& wheelsData, const PxVehicleDriveSimData& driveData,
		 const PxU32 numDrivenWheels);

	/**
	\brief Create a tank with 
	\brief (i)	a PxPhysics instance - needed to setup special vehicle constraints maintained by the vehicle.
	\brief (ii)	a PxRigidDynamic instance - the rigid body representation of the tank in the physx sdk.
	\brief (iii)a PxVehicleWheelsSimData instance describing the wheel/suspension/tires - the tank instance takes a copy of this input data.
	\brief (iv)	a PxVehicleDriveSimData instance describing the drive data - the tank instance takes a copy of this input data.
	\brief (v)	the number of driven wheels (the number of tank wheels)
	\brief (vi)	it is assumed that the first numDrivenWheel shapes of vehActor correspond to the wheel shapes of the tank
	\brief (vii)it is assumed that the even-numbered wheel shapes are the left-hand wheels ordered in sequence from front to back
	\brief (viii)it is assumed that the odd-numbered wheel shapes are the right-hand wheels ordered in sequence from front to back 
	\brief (ix)	it is assumed that the N non-driven wheel shapes are shapes 4 -> 4+N-1 of the actor.
	\brief (x)	it is assumed that the ordering of the wheel shapes in the actor matches the ordering in PxVehicleWheelsSimData.
	\brief (xi)	to break assumptions (vi)-(x) see PxVehicleWheels::setWheelShapeMapping
	@see free, setToRestState, setup
	*/
	static PxVehicleDriveTank* create
		(PxPhysics* physics, PxRigidDynamic* vehActor, 
		 const PxVehicleWheelsSimData& wheelsData, const PxVehicleDriveSimData& driveData,
		 const PxU32 numDrivenWheels);

	/**
	\brief Set driving model
	\brief eDRIVE_MODEL_STANDARD: turning achieved by braking on one side, accelerating on the other side.
	\brief eDRIVE_MODEL_SPECIAL: turning achieved by accelerating forwards on one side, accelerating backwards on the other side.
	\brief default value is eDRIVE_MODEL_STANDARD
	*/
	void setDriveModel(const eDriveModel driveModel)
	{
		mDriveModel=driveModel;
	}

	/**
	\brief Return the driving model
	*/
	eDriveModel getDriveModel() const {return mDriveModel;}

	/**
	\brief Set a vehicle to its rest state.
	\brief Call after setup
	@see setup
	*/
	void setToRestState();

	/**
	\brief Simulation data that models vehicle components
	@see setup
	*/
	PxVehicleDriveSimData mDriveSimData;

private:

	PxVehicleDriveTank()
		: mDriveModel(eDRIVE_MODEL_STANDARD)
	{
	}

	~PxVehicleDriveTank()
	{
	}

	/**
	\brief Test if the instanced dynamics and configuration data has legal values.
	*/
	bool isValid() const;

	/**
	\brief Drive model
	@see setDriveModel, eDriveModel
	*/
	eDriveModel mDriveModel;

	PxU32 mPad[3];
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDriveTank) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_DRIVE_TANK_H

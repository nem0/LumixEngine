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

#ifndef PX_VEHICLE_DRIVE_H
#define PX_VEHICLE_DRIVE_H
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
struct PxRaycastQueryResult;
class PxVehicleDrivableSurfaceToTireFrictionPairs;
class PxShape;
class PxMaterial;
class PxRigidDynamic;

/**
\brief Data structure describing non-wheel configuration data of a vehicle that has engine, gears, clutch, and auto-box.
@see PxVehicleWheelsSimData for wheels configuration data.
*/
class PxVehicleDriveSimData
{

public:

	friend class PxVehicleDriveTank;

	/**
	\brief Return the engine data
	*/
	PX_FORCE_INLINE const PxVehicleEngineData& getEngineData() const 
	{
		return mEngine;
	}

	/**
	\brief Return the gears data
	*/
	PX_FORCE_INLINE const PxVehicleGearsData& getGearsData() const 
	{
		return mGears;
	}

	/**
	\brief Return the clutch data
	*/
	PX_FORCE_INLINE const PxVehicleClutchData& getClutchData() const 
	{
		return mClutch;
	}

	/**
	\brief Return the autobox data
	*/
	PX_FORCE_INLINE const PxVehicleAutoBoxData& getAutoBoxData() const 
	{
		return mAutoBox;
	}

	/**
	\brief Set the engine data
	*/
	void setEngineData(const PxVehicleEngineData& engine);

	/**
	\brief Set the gears data
	*/
	void setGearsData(const PxVehicleGearsData& gears);

	/**
	\brief Set the clutch datta
	*/
	void setClutchData(const PxVehicleClutchData& clutch);

	/**
	\brief Set the autobox data
	*/
	void setAutoBoxData(const PxVehicleAutoBoxData& autobox);

protected:

	/*
	\brief Engine simulation data
	@see setEngineData, getEngineData
	*/
	PxVehicleEngineData				mEngine;

	/*
	\brief Gear simulation data
	@see setGearsData, getGearsData
	*/
	PxVehicleGearsData				mGears;

	/*
	\brief Clutch simulation data
	@see setClutchData, getClutchData
	*/
	PxVehicleClutchData				mClutch;

	/*
	\brief Autobox simulation data
	@see setAutoboxData, getAutoboxData
	*/
	PxVehicleAutoBoxData			mAutoBox;

	/**
	\brief Test that a PxVehicleDriveSimData instance has been configured with legal data.
	\brief Call only after setting all components with setEngineData,setGearsData,setClutchData,setAutoBoxData
	@see PxVehicleDrive4W::setup, PxVehicleDriveTank::setup
	*/
	bool isValid() const;

};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDriveSimData) & 15));


/**
\brief Data structure with instanced dynamics data for vehicle with engine, clutch, gears, autobox
@see PxVehicleWheelsDynData for wheels dynamics data.
*/
class PxVehicleDriveDynData
{
public:

	friend class PxVehicleDrive;

	PxVehicleDriveDynData();

	/**
	\brief Set all dynamics data to zero to bring the vehicle to rest.
	*/
	void setToRestState();

	/**
	\brief Set an analog control value to drive the vehicle.
	*/
	void setAnalogInput(const PxReal analogVal, const PxU32 type);

	/**
	\brief Get the analog control value that has been applied to the vehicle.
	*/
	PxReal getAnalogInput(const PxU32 type);

	/**
	\brief Set that the gearup button has been pressed
	*/
	void setGearUp(const bool digitalVal) 
	{
		mGearUpPressed = digitalVal;
	}

	/**
	\brief Set that the geardown button has been pressed
	*/
	void setGearDown(const bool digitalVal) 
	{
		mGearDownPressed = digitalVal;
	}

	/**
	\brief Check if the gearup button has been pressed
	*/
	bool getGearUp() const 
	{
		return mGearUpPressed;
	}

	/**
	\brief Check if the geardown button has been pressed
	*/
	bool getGearDown() const 
	{
		return mGearDownPressed;
	}

	/**
	\brief Get the flag status that is used to select auto-gears
	*/
	PX_FORCE_INLINE bool getUseAutoGears() const
	{
		return mUseAutoGears;
	}

	/**
	\brief Toggle the autogears flag
	*/
	PX_FORCE_INLINE void toggleAutoGears() 
	{
		mUseAutoGears = !mUseAutoGears;
	}

	/**
	\brief Get the current gear
	*/
	PX_FORCE_INLINE PxU32 getCurrentGear() const
	{
		return mCurrentGear;
	}

	/**
	\brief Get the target gear
	*/
	PX_FORCE_INLINE PxU32 getTargetGear() const
	{
		return mTargetGear;
	}
 
	/**
	\brief Start a gear change to a target gear
	*/
	PX_FORCE_INLINE void startGearChange(const PxU32 targetGear)
	{
		mTargetGear=targetGear;
	}

	/**
	\brief Force an immediate gear change to a target gear
	*/
	PX_FORCE_INLINE void forceGearChange(const PxU32 targetGear)
	{
		mTargetGear=targetGear;
		mCurrentGear=targetGear;
	}

	/**
	\brief Return the rotation speed of the engine.
	*/
	PX_FORCE_INLINE PxReal getEngineRotationSpeed() const
	{
		return mEnginespeed;
	}

	/**
	\brief Set the flag that will be used to select auto-gears
	*/
	PX_FORCE_INLINE void setUseAutoGears(const bool useAutoGears)
	{
		mUseAutoGears=useAutoGears;
	}

public:

	enum
	{
		eANALOG_INPUT_ACCEL=0,
		eMAX_NUM_ANALOG_INPUTS=16
	};

	/**
	\brief All dynamic data values are public for fast access.
	*/

	/**
	\brief Analog control values used by vehicle simulation.
	\brief Accelerator pedal value used for vehicle simulation is equal to mControlAnalogVals[eVEHICLE_ANALOG_INPUT_ACCEL].
	\brief Brake pedal value used by vehicle simulation is equal to mControlAnalogVals[eVEHICLE_ANALOG_INPUT_BRAKE].
	\brief Handbrake value used by vehicle simulation is equal to mControlAnalogVals[eVEHICLE_ANALOG_INPUT_HANDBRAKE].
	\brief Steer value used by vehicle simulation is equal to mControlAnalogVals[eVEHICLE_ANALOG_INPUT_STEER_RIGHT]-mControlAnalogVals[eVEHICLE_ANALOG_INPUT_STEER_LEFT].
	@see setAnalogInput, getAnalogInput
	*/
	PxReal mControlAnalogVals[eMAX_NUM_ANALOG_INPUTS];

	/**
	\brief Autogear flag used by vehicle simulation.  Set true to enable the autobox, false to disable the autobox.
	@see setUseAutoGears, setUseAutoGears, toggleAutoGears
	*/
	bool mUseAutoGears;

	/**
	\brief Gearup digital control value used by vehicle simulation.  If true a gear change will be initiated towards currentGear+1 (or to first gear if in reverse).
	@see setDigitalInput, getDigitalInput
	*/
	bool mGearUpPressed;

	/**
	\brief Geardown digital control value used by vehicle simulation.  If true a gear change will be initiated towards currentGear-1 (or to reverse if in first).
	@see setDigitalInput, getDigitalInput
	*/
	bool mGearDownPressed;

	/**
	\brief Current gear 
	@see startGearChange, forceGearChange, getCurrentGear
	*/
	PxU32 mCurrentGear;

	/**
	\brief Target gear (different from current gear if a gear change is underway) 
	@see startGearChange, forceGearChange, getTargetGear
	*/
	PxU32 mTargetGear;

	/**
	\brief Rotation speed of engine
	@see setToRestState, getEngineRotationSpeed
	*/	
	PxReal mEnginespeed;

	/**
	\brief Reported time that has passed since gear change started.
	@see setToRestState, startGearChange
	*/
	PxReal mGearSwitchTime;

	/**
	\brief Reported time that has passed since last autobox gearup/geardown decision.
	@see setToRestState
	*/
	PxReal mAutoBoxSwitchTime;

private:

	PxU32 mPad[2];

	/**
	\brief Test that a PxVehicleDriveDynData instance has legal values.
	@see setToRestState
	*/
	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDriveDynData) & 15));

/**
\brief A complete vehicle with instance dynamics data and configuration data for wheels and engine,clutch,gears,autobox.
@see PxVehicleDrive4W, PxVehicleDriveTank
*/
class PxVehicleDrive : public PxVehicleWheels
{
public:

	friend class PxVehicleUpdate;

	/**
	\brief Dynamics data of vehicle instance.
	@see setup
	*/
	PxVehicleDriveDynData mDriveDynData;

protected:

	PxVehicleDrive()
	{
	}

	~PxVehicleDrive()
	{
	}

	/**
	\brief Test that all instanced dynamics data and configuration data have legal values.
	*/
	bool isValid() const;

	/**
	\brief Set vehicle to rest.
	*/
	void setToRestState();

	/**
	@see PxVehicleDrive4W::allocate, PxVehicleDriveTank::allocate
	*/
	static PxU32 computeByteSize(const PxU32 numWheels4);

	/**
	@see PxVehicleDrive4W::allocate, PxVehicleDriveTank::allocate
	*/
	static PxU8* patchupPointers(PxVehicleDrive* vehDrive, PxU8* ptr, const PxU32 numWheels4, const PxU32 numWheels);

	/**
	\brief Deallocate a PxVehicle4WDrive instance.
	@see PxVehicleDrive4W::free, PxVehicleDriveTank::free
	*/
	void free();

	/**
	@see PxVehicleDrive4W::setup, PxVehicleDriveTank::setup
	*/
	void setup
		(PxPhysics* physics, PxRigidDynamic* vehActor, 
		 const PxVehicleWheelsSimData& wheelsData,
		 const PxU32 numDrivenWheels, const PxU32 numNonDrivenWheels);
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDrive) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_DRIVE_H

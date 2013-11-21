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

#ifndef PX_VEHICLE_4WDRIVE_H
#define PX_VEHICLE_4WDRIVE_H
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
\brief Data structure describing configuration data of a vehicle with up to 4 driven wheels,
\brief up to 16 undriven wheels,
\brief and engine, clutch, gears, autobox, differential, and Ackermann steer correction.
@see PxVehicleDriveSimData
*/
class PxVehicleDriveSimData4W : public PxVehicleDriveSimData
{
public:

	friend class PxVehicleDrive4W;

	/**
	\brief Return the data describing the differential of a vehicle with up to 4 driven wheels.
	*/
	PX_FORCE_INLINE const PxVehicleDifferential4WData& getDiffData() const 
	{
		return mDiff;
	}

	/**
	\brief Return the data describing the Ackerkmann steer-correction of a vehicle with up to 4 driven wheels.
	*/
	PX_FORCE_INLINE const PxVehicleAckermannGeometryData& getAckermannGeometryData() const 
	{
		return mAckermannGeometry;
	}

	/**
	\brief Set the data describing the differential of a vehicle with up to 4 driven wheels.
	*/
	void setDiffData(const PxVehicleDifferential4WData& diff);

	/**
	\brief Set the data describing the Ackerkmann steer-correction of a vehicle with up to 4 driven wheels.
	*/
	void setAckermannGeometryData(const PxVehicleAckermannGeometryData& ackermannData);

private:

	/**
	\brief Differential simulation data
	@see setDiffData, getDiffData
	*/
	PxVehicleDifferential4WData		mDiff;

	/**
	\brief Data for ackermann steer angle computation.
	@see setAckermannGeometryData, getAckermannGeometryData
	*/
	PxVehicleAckermannGeometryData	mAckermannGeometry;

	/**
	\brief Test if the 4W-drive simulation data has been setup with legal data.
	\brief Call only after setting all components.
	@see setEnginedata, setClutchData, setGearsData, setAutoboxData, setDiffData, setAckermannGeometryData 
	*/
	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDriveSimData4W) & 15));


/**
\brief Data structure with instanced dynamics data and configuration data of a vehicle with up to 4 driven wheels.
\brief and up to 16 non-driven wheels.
*/
class PxVehicleDrive4W : public PxVehicleDrive
{
public:

	friend class PxVehicleUpdate;

	/**
	\brief The ordering of the driven and steered wheels
	*/
	enum eWheelOrdering
	{
		eFRONT_LEFT_WHEEL=0,
		eFRONT_RIGHT_WHEEL,
		eREAR_LEFT_WHEEL,
		eREAR_RIGHT_WHEEL
	};

	/**
	@see PxVehicleDrive::setAnalogInput, PxVehicleDrive::getAnalogInput
	*/
	enum
	{
		eANALOG_INPUT_ACCEL=PxVehicleDriveDynData::eANALOG_INPUT_ACCEL,		
		eANALOG_INPUT_BRAKE,		
		eANALOG_INPUT_HANDBRAKE,	
		eANALOG_INPUT_STEER_LEFT,	
		eANALOG_INPUT_STEER_RIGHT,	
		eMAX_NUM_DRIVE4W_ANALOG_INPUTS
	};

	/**
	\brief Allocate a PxVehicleDrive4W instance for a 4WDrive vehicle with numWheels (= num driven wheels + num undriven wheels)
	@see free, setup
	*/
	static PxVehicleDrive4W* allocate(const PxU32 numWheels);

	/**
	\brief Deallocate a PxVehicleDrive4W instance.
	@see allocate
	*/
	void free();

	/**
	\brief Set up a vehicle with 
	\brief(i)	a PxPhysics instance - needed to setup special vehicle constraints maintained by the vehicle.
	\brief(ii)	a PxRigidDynamic instance - the rigid body representation of the vehicle in the physx sdk.
	\brief(iii)	a PxVehicleWheelsSimData instance describing the wheel/suspension/tires - the vehicle instance takes a copy of this input data.
	\brief(iv)	a PxVehicle4WDriveSimData instance describing the drive data (engine/Ackermann steer correction/differential etc) - the vehicle instance takes a copy of this input data.
	\brief(v)	the number of non-driven wheels (it is assumed that the vehicle has up to 4 wheels that could be driven by the engine).
	\brief(vi)	it is assumed that the first shapes of the actor are the wheel shapes, followed by the chassis shapes.
	\brief(viii)it is assumed that the front-left wheel shape is shape 0 of the actor
	\brief(ix)	it is assumed that the front-right wheel shape is shape 1 of the actor
	\brief(x)	it is assumed that the rear-left wheel shape is shape 2 of the actor
	\brief(xi)	it is assumed that the rear-right wheel shape is shape 3 of the actor
	\brief(xii)  it is assumed that the N non-driven wheel shapes are shapes 4 -> 4+N-1 of the actor.
	\brief(xiii)it is assumed that the wheel shapes ordered in the actor match the wheels specified in PxVehicleWheelsSimData.
	\brief(xiv)	to break assumptions (vi)-(xiii) and have arbitary shape ordering see PxVehicleWheels::setWheelShapeMapping
	\brief(xiv)	only the first four wheels (the driven wheels) are connected to the steering.
	@see allocate, free, setToRestState, eWheelOrdering
	*/
	void setup
		(PxPhysics* physics, PxRigidDynamic* vehActor,
		 const PxVehicleWheelsSimData& wheelsData, const PxVehicleDriveSimData4W& driveData,
		 const PxU32 numNonDrivenWheels);

	/**
	\brief Set up a vehicle with 
	\brief(i)	a PxPhysics instance - needed to setup special vehicle constraints maintained by the vehicle.
	\brief(ii)	a PxRigidDynamic instance - the rigid body representation of the vehicle in the physx sdk.
	\brief(iii)	a PxVehicleWheelsSimData instance describing the wheel/suspension/tires - the vehicle instance takes a copy of this input data.
	\brief(iv)	a PxVehicle4WDriveSimData instance describing the drive data (engine/Ackermann steer correction/differential etc) - the vehicle instance takes a copy of this input data.
	\brief(v)	the number of non-driven wheels (it is assumed that the vehicle has up to 4 wheels that could be driven by the engine).
	\brief(vi)	it is assumed that the first shapes of the actor are the wheel shapes, followed by the chassis shapes.
	\brief(viii)it is assumed that the front-left wheel shape is shape 0 of the actor
	\brief(ix)	it is assumed that the front-right wheel shape is shape 1 of the actor
	\brief(x)	it is assumed that the rear-left wheel shape is shape 2 of the actor
	\brief(xi)	it is assumed that the rear-right wheel shape is shape 3 of the actor
	\brief(xii) it is assumed that the N non-driven wheel shapes are shapes 4 -> 4+N-1 of the actor.
	\brief(xiii)it is assumed that the wheel shapes ordered in the actor match the wheels specified in PxVehicleWheelsSimData.
	\brief(xiv)	to break assumptions (vi)-(xiii) and have arbitary shape ordering see PxVehicleWheels::setWheelShapeMapping
	\brief(xv)	only the first four wheels (the driven wheels) are connected to the steering.
	@see free, setToRestState, setup, eWheelOrdering
	*/
	static PxVehicleDrive4W* create
		(PxPhysics* physics, PxRigidDynamic* vehActor,
		 const PxVehicleWheelsSimData& wheelsData, const PxVehicleDriveSimData4W& driveData,
		 const PxU32 numNonDrivenWheels);

	/**
	\brief Set a vehicle to its rest state.
	@see setup
	*/
	void setToRestState();

	/**
	\brief Simulation data that models vehicle components
	@see setup
	*/
	PxVehicleDriveSimData4W mDriveSimData;

private:

	PxVehicleDrive4W()
	{
	}

	~PxVehicleDrive4W()
	{
	}

	/**
	\brief Test if the instanced dynamics and configuration data has legal values.
	*/
	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDrive4W) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_4WDRIVE_H

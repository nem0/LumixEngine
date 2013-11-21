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


#ifndef PX_VEHICLE_WHEELS_H
#define PX_VEHICLE_WHEELS_H
/** \addtogroup vehicle
  @{
*/

#include "foundation/PxSimpleTypes.h"
#include "vehicle/PxVehicleShaders.h"
#include "vehicle/PxVehicleComponents.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVehicleWheels4SimData;
class PxVehicleWheels4DynData;
class PxRigidDynamic;
class PxShape;
class PxPhysics;
class PxMaterial;

/**
\brief Data structure describing configuration data of a vehicle with up to 20 wheels,
*/

class PxVehicleWheelsSimData
{
public:

	friend class PxVehicleWheels;
	friend class PxVehicleDrive4W;
	friend class PxVehicleDriveTank;
	friend class PxVehicleUpdate;

	/**
	\brief Allocate a PxVehicleWheelsSimData instance for with numWheels.
	@see free
	*/
	static PxVehicleWheelsSimData* allocate(const PxU32 numWheels);

	/**
	\brief Setup with mass information that can be applied to the default values of the suspensions, wheels, and tires
	set in their respective constructors.
	This function assumes that the suspensions equally share the load of the chassis mass.  It also
	assumes that the suspension will have a particular natural frequency and damping ratio that is typical
	of a standard car.  If either of these assumptions is broken then each suspension will need to 
	be individually configured with custom strength, damping rate, and sprung mass.
	@see allocate
	*/
	void setChassisMass(const PxF32 chassisMass);

	/**
	\brief Free a PxVehicleWheelsSimData instance 
	@see allocate
	*/
	void free();

	/**
	\brief Copy wheel simulation data.
	*/
	PxVehicleWheelsSimData& operator=(const PxVehicleWheelsSimData& src);

	/**
	\brief Copy the data of a single wheel unit (wheel, suspension, tire) from srcWheel of src to trgWheel.
	*/
	void copy(const PxVehicleWheelsSimData& src, const PxU32 srcWheel, const PxU32 trgWheel);

	/**
	\brief Return the number of wheels 
	@see allocate
	*/
	PxU32 getNumWheels() const {return mNumActiveWheels;}

	/**
	\brief Return the suspension data of the idth wheel
	*/
	const PxVehicleSuspensionData& getSuspensionData(const PxU32 id) const;

	/**
	\brief Return the wheel data of the idth wheel
	*/
	const PxVehicleWheelData& getWheelData(const PxU32 id) const;

	/**
	\brief Return the tire data of the idth wheel
	*/
	const PxVehicleTireData& getTireData(const PxU32 id) const;

	/**
	\brief Return the direction of travel of the suspension of the idth wheel
	*/
	const PxVec3& getSuspTravelDirection(const PxU32 id) const;

	/**
	\brief Return the application point of the suspension force of the suspension of the idth wheel.
	\brief Specified relative to the centre of mass of the rigid body
	*/
	const PxVec3& getSuspForceAppPointOffset(const PxU32 id) const;

	/**
	\brief Return the application point of the tire force of the tire of the idth wheel.
	\brief Specified relative to the centre of mass of the rigid body
	*/
	const PxVec3& getTireForceAppPointOffset(const PxU32 id) const;

	/**
	\brief Return the offset from the rigid body centre of mass to the centre of the idth wheel.
	*/
	const PxVec3& getWheelCentreOffset(const PxU32 id) const;	

	/**
	\brief Return the data that describes the filtering of the tire load to produce smoother handling at large timesteps.
	*/
	PX_FORCE_INLINE const PxVehicleTireLoadFilterData& getTireLoadFilterData() const 
	{
		return mNormalisedLoadFilter;
	}

	/**
	\brief Set the suspension data of the idth wheel
	*/
	void setSuspensionData(const PxU32 id, const PxVehicleSuspensionData& susp);

	/**
	\brief Set the wheel data of the idth wheel
	*/
	void setWheelData(const PxU32 id, const PxVehicleWheelData& wheel);

	/**
	\brief Set the tire data of the idth wheel
	*/
	void setTireData(const PxU32 id, const PxVehicleTireData& tire);

	/**
	\brief Set the direction of travel of the suspension of the idth wheel
	*/
	void setSuspTravelDirection(const PxU32 id, const PxVec3& dir);
	
	/**
	\brief Set the application point of the suspension force of the suspension of the idth wheel.
	\brief Specified relative to the centre of mass of the rigid body
	*/
	void setSuspForceAppPointOffset(const PxU32 id, const PxVec3& offset);									
	
	/**
	\brief Set the application point of the tire force of the tire of the idth wheel.
	\brief Specified relative to the centre of mass of the rigid body
	*/
	void setTireForceAppPointOffset(const PxU32 id, const PxVec3& offset);

	/**
	\brief Set the offset from the rigid body centre of mass to the centre of the idth wheel.
	*/
	void setWheelCentreOffset(const PxU32 id, const PxVec3& offset);									

	/**
	\brief Set the data that describes the filtering of the tire load to produce smoother handling at large timesteps.
	*/
	void setTireLoadFilterData(const PxVehicleTireLoadFilterData& tireLoadFilter);

	/**
	\brief Disable a wheel so that zero suspension forces and zero tire forces are applied to the rigid body from this wheel.
	*/
	void disableWheel(const PxU32 wheel);

	/**
	\brief Test if a wheel has been disabled.
	*/
	bool getIsWheelDisabled(const PxU32 wheel) const;

private:

	PxVehicleWheelsSimData(){}
	~PxVehicleWheelsSimData(){}

	/**
	\brief Graph to filter normalised load
	@see setTireLoadFilterData, getTireLoadFilterData
	*/
	PxVehicleTireLoadFilterData mNormalisedLoadFilter;

	/**
	\brief Wheels data organised in blocks of 4 wheels.
	*/
	PxVehicleWheels4SimData* mWheels4SimData;

	/**
	\brief Number of blocks of 4 wheels.
	*/
	PxU32 mNumWheels4;

	/**
	\brief Number of actual wheels (<=(mNumWheels4*4))
	*/
	PxU32 mNumActiveWheels;

#ifdef PX_X64
	//PxU32 mPad[0];
#else
	PxU32 mPad[1];
#endif

	/**
	\brief Test if wheel simulation data has been setup with legal values.
	*/
	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleWheelsSimData) & 15));


/**
\brief Data structure with instanced dynamics data for wheels
*/
class PxVehicleWheelsDynData
{
public:

	friend class PxVehicleWheels;
	friend class PxVehicleDrive4W;
	friend class PxVehicleDriveTank;
	friend class PxVehicleUpdate;

	/**
	\brief Set all wheels to their rest state.
	@see setup
	*/
	void setToRestState();

	/**
	\brief Set the tire force shader function
	*/
	void setTireForceShaderFunction(PxVehicleComputeTireForce tireForceShaderFn);

	/**
	\brief Set the tire force shader data for a specific tire
	*/
	void setTireForceShaderData(const PxU32 tireId, const void* tireForceShaderData);

	/**
	\brief Return the rotation speed of a specified wheel.
	*/
	PxReal getWheelRotationSpeed(const PxU32 wheelIdx) const;

	/**
	\brief Return the steer angle of a specified wheel.
	*/
	PxReal getSteer(const PxU32 wheelIdx) const;

	/**
	\brief Return the rotation angle of the specified wheel about the axis of rolling rotation.
	*/
	PxReal getWheelRotationAngle(const PxU32 wheelIdx) const;

	/**
	\brief Return the susp jounce of the specified suspension.
	*/
	PxReal getSuspJounce(const PxU32 suspIdx) const;

	/**
	\brief Return the longitudinal slip of the specified tire
	*/
	PxReal getTireLongSlip(const PxU32 tireIdx) const;

	/**
	\brief Return the lateral slip of the specified tire
	*/
	PxReal getTireLatSlip(const PxU32 tireIdx) const;

	/**
	\brief Return the friction applied to the specified tire
	*/
	PxReal getTireFriction(const PxU32 tireIdx) const;

	/**
	\brief Return the drivable surface type underneath the specified tire
	*/
	PxU32 getTireDrivableSurfaceType(const PxU32 tireIdx) const;

	/**
	\brief Return the PxMaterial of the drivable surface underneath the specified tire
	*/
	const PxMaterial* getTireDrivableSurfaceMaterial(const PxU32 tireIdx) const;

	/**
	\brief Return the raycast start, direction, and length of the specified suspension line.
	*/
	void getSuspRaycast(const PxU32 suspIdx, PxVec3& start, PxVec3& dir, PxReal& length) const;

	/**
	\brief Return the suspension line start
	*/
	PxVec3 getSuspLineStart(const PxU32 tireIdx) const;

	/**
	\brief Return the suspension line dir
	*/
	PxVec3 getSuspLineDir(const PxU32 tireIdx) const;

private:

	/**
	\brief Test if wheel dynamics data have legal values.
	*/
	bool isValid() const;

	PxVehicleWheelsDynData(){}
	~PxVehicleWheelsDynData(){}

	/**
	\brief Dynamics data arranged in blocks of 4 wheels.
	*/
	PxVehicleWheels4DynData* mWheels4DynData;

	/**
	\brief Shader data and function for tire force calculations.
	*/
	PxVehicleTireForceCalculator* mTireForceCalculators;

	/**
	\brief Number of blocks of 4 wheels.
	*/
	PxU32 mNumWheels4;

	/**
	\brief Number of wheels (mNumActiveWheels <= (mNumWheels4*4))
	*/
	PxU32 mNumActiveWheels;

#ifdef PX_X64
	PxU32 mPad[1];
#endif
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleWheelsDynData) & 15));

/**
\brief Data structure with instanced dynamics data and configuration data of a vehicle with just wheels
@see PxVehicleDrive, PxVehicleDrive4W, PxVehicleDriveTank
*/
class PxVehicleWheels
{
public:

	friend class PxVehicleUpdate;
	friend class PxVehicleConstraintShader;

	/**
	\brief Return the type of vehicle 
	@see 
	*/
	PX_FORCE_INLINE PxU32 getVehicleType() const {return mType;}

	/**
	\brief Get non-const ptr to PxRigidDynamic instance that is the vehicle's physx representation
	*/
	PX_FORCE_INLINE PxRigidDynamic* getRigidDynamicActor() {return mActor;}

	/**
	\brief Get non-const ptr to PxRigidDynamic instance that is the vehicle's physx representation
	*/
	PX_FORCE_INLINE const PxRigidDynamic* getRigidDynamicActor() const {return mActor;}

	/**
	\brief Set mapping between wheel id and position of corresponding wheel shape in the list of actor shapes.
	This mapping is used to pose the correct wheel shapes with the latest wheel rotation angle, steer angle, and suspension travel
	and allows arbitrary ordering of the wheel shapes in the actor's list of shapes.
	Use setWheelShapeMapping(i,-1) to register that there is no wheel shape corresponding to the ith wheel
	Set setWheelShapeMapping(i,k) to register that the ith wheel corresponds to the kth shape in the actor's list of shapes.
	Default value are provided in the setup functions.  The default values correspond to setWheelShapeMapping(i,i) for all i<numWheels
	@see PxVehicleUpdates, PxVehicleDrive4W::setup, PxVehicleDriveTank::setup
	*/
	void setWheelShapeMapping(const PxU32 wheelId, const PxI32 shapeId);

	/**
	\brief Compute the rigid body velocity component along the forward vector.
	*/
	PxReal computeForwardSpeed() const;

	/**
	\brief Compute the rigid body velocity component along the right vector.
	*/
	PxReal computeSidewaysSpeed() const;

	/**
	\brief Test if all wheels are off the ground.
	*/
	bool isInAir() const;

	/**
	\brief Test if a specific wheel is off the ground.
	*/
	bool isInAir(const PxU32 wheelId) const;

	/**
	\brief Data describing the setup of all the wheels/suspensions/tires.
	*/
	PxVehicleWheelsSimData mWheelsSimData;

	/**
	\brief Data describing the dynamic state of all wheels/suspension/tires.
	*/
	PxVehicleWheelsDynData mWheelsDynData;	

protected:

	/**
	\brief Set all wheels to their rest state
	*/
	void setToRestState();

	/**
	\brief Test that all configuration and instanced dynamics data is valid.
	*/
	bool isValid() const;

	/**
	@see PxVehicleDrive4W::allocate, PxVehicleDriveTank::allocate
	*/
	static PxU32 computeByteSize(const PxU32 numWheels4);

	/**
	@see PxVehicleDrive4W::allocate, PxVehicleDriveTank::allocate
	*/
	static PxU8* patchupPointers(PxVehicleWheels* veh, PxU8* ptr, const PxU32 numWheels4, const PxU32 numWheels);

	/**
	\brief Deallocate a PxVehicleWheels instance.
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

	PxRigidDynamic* mActor;

	/**
	\brief Mapping between wheel id and shape id.
	*/
	PxU8 mWheelShapeMap[PX_MAX_NUM_WHEELS];

	/**
	\brief Vehicle type (eVehicleDriveTypes)
	*/
	PxU8 mType;

private:

	/**
	\brief Count the number of constraint connectors that have hit their callback when deleting a vehicle.
	Can only delete the vehicle's memory when all constraint connectors have hit their callback.
	*/
	PxU8 mOnConstraintReleaseCounter;

	PxU8 mPad[2];

#ifndef PX_X64
	PxU32 mPad2[1];
#endif
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleWheels) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_WHEELS_H

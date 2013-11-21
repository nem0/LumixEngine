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

#ifndef PX_VEHICLE_CONTROL_H
#define PX_VEHICLE_CONTROL_H
/** \addtogroup vehicle
  @{
*/
#include "vehicle/PxVehicleSDK.h"
#include "vehicle/PxVehicleDrive4W.h"
#include "vehicle/PxVehicleDriveTank.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVehicleDrive4W;

/**
\brief Used to produce smooth vehicle driving control values from key inputs.
@see PxVehicle4WSmoothDigitalRawInputsAndSetAnalogInputs, PxVehicle4WSmoothAnalogRawInputsAndSetAnalogInputs
*/
struct PxVehicleKeySmoothingData
{
public:

	/**
	\brief Rise rate of each analog value if digital value is 1
	*/
	PxReal mRiseRates[PxVehicleDriveDynData::eMAX_NUM_ANALOG_INPUTS];

	/**
	\brief Fall rate of each analog value if digital value is 0
	*/
	PxReal mFallRates[PxVehicleDriveDynData::eMAX_NUM_ANALOG_INPUTS];
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleKeySmoothingData)& 0x0f));

/**
\brief Used to produce smooth analog vehicle control values from analog inputs.
@see PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs, PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs
*/
struct PxVehiclePadSmoothingData
{
public:

	/**
	\brief Rise rate of each analog value from previous value towards target if target>previous
	*/
	PxReal mRiseRates[PxVehicleDriveDynData::eMAX_NUM_ANALOG_INPUTS];

	/**
	\brief Rise rate of each analog value from previous value towards target if target<previous
	*/
	PxReal mFallRates[PxVehicleDriveDynData::eMAX_NUM_ANALOG_INPUTS];
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehiclePadSmoothingData)& 0x0f));

/**
\brief Used to produce smooth vehicle driving control values from analog inputs.
@see PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs, PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs
*/
class PxVehicleDrive4WRawInputData
{
public:

	PxVehicleDrive4WRawInputData()
	{
		for(PxU32 i=0;i<PxVehicleDrive4W::eMAX_NUM_DRIVE4W_ANALOG_INPUTS;i++)
		{
			mRawDigitalInputs[i]=false;
			mRawAnalogInputs[i]=0.0f;
		}

		mGearUp = false;
		mGearDown = false;
	}

	~PxVehicleDrive4WRawInputData()
	{
	}

	/**
	\brief Record if the accel button has been pressed on keyboard.
	*/
	void setDigitalAccel(const bool accelKeyPressed)			{mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_ACCEL]=accelKeyPressed;}

	/**
	\brief Record if the brake button has been pressed on keyboard.
	*/
	void setDigitalBrake(const bool brakeKeyPressed)			{mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_BRAKE]=brakeKeyPressed;}

	/**
	\brief Record if the handbrake button has been pressed on keyboard.
	*/
	void setDigitalHandbrake(const bool handbrakeKeyPressed)	{mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_HANDBRAKE]=handbrakeKeyPressed;}

	/**
	\brief Record if the left steer button has been pressed on keyboard.
	*/
	void setDigitalSteerLeft(const bool steerLeftKeyPressed)	{mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_LEFT]=steerLeftKeyPressed;}

	/**
	\brief Record if the right steer button has been pressed on keyboard.
	*/
	void setDigitalSteerRight(const bool steerRightKeyPressed)	{mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_RIGHT]=steerRightKeyPressed;}


	/**
	\brief Return if the accel button has been pressed on keyboard.
	*/
	bool getDigitalAccel() const								{return mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_ACCEL];}

	/**
	\brief Return if the brake button has been pressed on keyboard.
	*/
	bool getDigitalBrake() const								{return mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_BRAKE];}

	/**
	\brief Return if the handbrake button has been pressed on keyboard.
	*/
	bool getDigitalHandbrake() const							{return mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_HANDBRAKE];}

	/**
	\brief Return if the left steer button has been pressed on keyboard.
	*/
	bool getDigitalSteerLeft() const							{return mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_LEFT];}

	/**
	\brief Return if the right steer button has been pressed on keyboard.
	*/
	bool getDigitalSteerRight() const							{return mRawDigitalInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_RIGHT];}


	/**
	\brief Set the analog accel value from the gamepad
	*/
	void setAnalogAccel(const PxReal accel)						{mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_ACCEL]=accel;}

	/**
	\brief Set the analog brake value from the gamepad
	*/
	void setAnalogBrake(const PxReal brake)						{mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_BRAKE]=brake;}

	/**
	\brief Set the analog handbrake value from the gamepad
	*/
	void setAnalogHandbrake(const PxReal handbrake)				{mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_HANDBRAKE]=handbrake;}

	/**
	\brief Set the analog steer value from the gamepad
	*/
	void setAnalogSteer(const PxReal steer)						{mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_RIGHT]=steer;}


	/**
	\brief Return the analog accel value from the gamepad
	*/
	PxReal getAnalogAccel() const								{return mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_ACCEL];}

	/**
	\brief Return the analog brake value from the gamepad
	*/
	PxReal getAnalogBrake() const								{return mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_BRAKE];}

	/**
	\brief Return the analog handbrake value from the gamepad
	*/
	PxReal getAnalogHandbrake() const							{return mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_HANDBRAKE];}

	/**
	\brief Return the analog steer value from the gamepad
	*/
	PxReal getAnalogSteer() const								{return mRawAnalogInputs[PxVehicleDrive4W::eANALOG_INPUT_STEER_RIGHT];}

	/**
	\brief Record if the gearup button has been pressed on keyboard or gamepad
	*/
	void setGearUp(const bool gearUpKeyPressed)					{mGearUp=gearUpKeyPressed;}

	/**
	\brief Record if the geardown button has been pressed on keyboard or gamepad
	*/
	void setGearDown(const bool gearDownKeyPressed)				{mGearDown=gearDownKeyPressed;}

	/**
	\brief Return if the gearup button has been pressed on keyboard or gamepad
	*/
	bool getGearUp() const										{return mGearUp;}

	/**
	\brief Record if the geardown button has been pressed on keyboard or gamepad
	*/
	bool getGearDown() const									{return mGearDown;}

private:

	bool mRawDigitalInputs[PxVehicleDrive4W::eMAX_NUM_DRIVE4W_ANALOG_INPUTS];
	PxReal mRawAnalogInputs[PxVehicleDrive4W::eMAX_NUM_DRIVE4W_ANALOG_INPUTS];

	bool mGearUp;
	bool mGearDown;
};

/**
\brief Used to smooth and set analog vehicle control values (accel,brake,handbrake,steer) from digital inputs (keyboard).
 Also used to set boolean gearup, geardown values.
*/
void PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs
	(const PxVehicleKeySmoothingData& keySmoothing, const PxFixedSizeLookupTable<8>& steerVsForwardSpeedTable,
	 const PxVehicleDrive4WRawInputData& rawInputData, 
	 const PxReal timestep, 
	 PxVehicleDrive4W& focusVehicle);

/**
\brief Used to smooth and set analog vehicle control values from analog inputs (gamepad).
Also used to set boolean gearup, geardown values.
*/
void PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs
	(const PxVehiclePadSmoothingData& padSmoothing, const PxFixedSizeLookupTable<8>& steerVsForwardSpeedTable,
	 const PxVehicleDrive4WRawInputData& rawInputData, 
	 const PxReal timestep, 
	 PxVehicleDrive4W& focusVehicle);


/**
\brief Used to produce smooth analog tank control values from analog inputs.
@see PxVehicleDriveTankSmoothDigitalRawInputsAndSetAnalogInputs, PxVehicleDriveTankSmoothAnalogRawInputsAndSetAnalogInputs
*/
class PxVehicleDriveTankRawInputData
{
public:

	PxVehicleDriveTankRawInputData(const PxVehicleDriveTank::eDriveModel mode)
		: mMode(mode)
	{
		for(PxU32 i=0;i<PxVehicleDriveTank::eMAX_NUM_DRIVETANK_ANALOG_INPUTS;i++)
		{
			mRawAnalogInputs[i]=0.0f;
			mRawDigitalInputs[i]=false;
		}

		mGearUp=false;
		mGearDown=false;
	}

	~PxVehicleDriveTankRawInputData()
	{
	}

	/**
	\brief Return the drive model (eDRIVE_MODEL_SPECIAL or eDRIVE_MODEL_STANDARD)
	*/
	PxVehicleDriveTank::eDriveModel getDriveModel() const
	{
		return mMode;
	}

	/**
	\brief Set if the accel button has been pressed on the keyboard
	*/
	void setDigitalAccel(const bool b)					{mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_ACCEL]=b;}

	/**
	\brief Set if the left thrust button has been pressed on the keyboard
	*/
	void setDigitalLeftThrust(const bool b)				{mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_LEFT]=b;}

	/**
	\brief Set if the right thrust button has been pressed on the keyboard
	*/
	void setDigitalRightThrust(const bool b)			{mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_RIGHT]=b;}

	/**
	\brief Set if the left brake button has been pressed on the keyboard
	*/
	void setDigitalLeftBrake(const bool b)				{mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_LEFT]=b;}

	/**
	\brief Set if the right brake button has been pressed on the keyboard
	*/
	void setDigitalRightBrake(const bool b)				{mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_RIGHT]=b;}

	/**
	\brief Return if the accel button has been pressed on the keyboard
	*/
	bool getDigitalAccel() const						{return mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_ACCEL];}

	/**
	\brief Return if the left thrust button has been pressed on the keyboard
	*/
	bool getDigitalLeftThrust() const					{return mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_LEFT];}

	/**
	\brief Return if the right thrust button has been pressed on the keyboard
	*/
	bool getDigitalRightThrust() const					{return mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_RIGHT];}

	/**
	\brief Return if the left brake button has been pressed on the keyboard
	*/
	bool getDigitalLeftBrake() const					{return mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_LEFT];}

	/**
	\brief Return if the right brake button has been pressed on the keyboard
	*/
	bool getDigitalRightBrake() const					{return mRawDigitalInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_RIGHT];}


	/**
	\brief Set the analog accel value from the gamepad	
	In range (0,1).
	*/
	void setAnalogAccel(const PxF32 accel)					
	{
		PX_ASSERT(accel>=-0.01f && accel<=1.01f);
		mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_ACCEL]=accel;
	}

	/**
	\brief Set the analog left thrust value from the gamepad
	In range (0,1) for standard mode, in range (-1,1) for special mode
	*/
	void setAnalogLeftThrust(const PxF32 leftAccel)			
	{
		PX_ASSERT(leftAccel>=-1.01f && leftAccel<=1.01f);
		PX_ASSERT(PxVehicleDriveTank::eDRIVE_MODEL_SPECIAL==mMode || leftAccel>=-0.1f);
		mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_LEFT]=leftAccel;
	}

	/**
	\brief Set the analog right thrust value from the gamepad
	In range (0,1) for standard mode, in range (-1,1) for special mode
	*/
	void setAnalogRightThrust(const PxF32 rightAccel)			
	{
		PX_ASSERT(rightAccel>=-1.01f && rightAccel<=1.01f);
		PX_ASSERT(PxVehicleDriveTank::eDRIVE_MODEL_SPECIAL==mMode || rightAccel>=-0.1f);
		mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_RIGHT]=rightAccel;
	}

	/**
	\brief Set the analog left brake value from the gamepad	
	In range (0,1).
	*/
	void setAnalogLeftBrake(const PxF32 leftBrake)			
	{
		PX_ASSERT(leftBrake>=-0.01f && leftBrake<=1.01f);
		mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_LEFT]=leftBrake;
	}

	/**
	\brief Set the analog right brake value from the gamepad	
	In range (0,1).
	*/
	void setAnalogRightBrake(const PxF32 rightBrake)			
	{
		PX_ASSERT(rightBrake>=-0.01f && rightBrake<=1.01f);
		mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_RIGHT]=rightBrake;
	}

	/**
	\brief Return the analog accel value from the gamepad
	*/
	PxF32 getAnalogAccel() const								
	{
		return mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_ACCEL];
	}

	/**
	\brief Return the analog left thrust value from the gamepad
	*/
	PxF32 getAnalogLeftThrust() const							
	{
		return mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_LEFT];
	}

	/**
	\brief Return the analog right thrust value from the gamepad
	*/
	PxF32 getAnalogRightThrust() const						
	{
		return mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_THRUST_RIGHT];
	}

	/**
	\brief Return the analog left brake value from the gamepad
	*/
	PxF32 getAnalogLeftBrake() const							
	{
		return mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_LEFT];
	}

	/**
	\brief Return the analog right brake value from the gamepad
	*/
	PxF32 getAnalogRightBrake() const							
	{
		return mRawAnalogInputs[PxVehicleDriveTank::eANALOG_INPUT_BRAKE_RIGHT];
	}

	/**
	\brief Record if the gearup button has been pressed on keyboard or gamepad
	*/
	void setGearUp(const bool gearUp)					{mGearUp=gearUp;}

	/**
	\brief Record if the geardown button has been pressed on keyboard or gamepad
	*/
	void setGearDown(const bool gearDown)				{mGearDown=gearDown;}

	/**
	\brief Return if the gearup button has been pressed on keyboard or gamepad
	*/
	bool getGearUp() const								{return mGearUp;}

	/**
	\brief Return if the geardown button has been pressed on keyboard or gamepad
	*/
	bool getGearDown() const							{return mGearDown;}

private:

	PxVehicleDriveTank::eDriveModel mMode;

	PxReal mRawAnalogInputs[PxVehicleDriveTank::eMAX_NUM_DRIVETANK_ANALOG_INPUTS];
	bool mRawDigitalInputs[PxVehicleDriveTank::eMAX_NUM_DRIVETANK_ANALOG_INPUTS];
	bool mGearUp;
	bool mGearDown;
};

/**
\brief Used to smooth and set analog tank control values from digital inputs (gamepad).
Also used to set boolean gearup, geardown values.
*/
void PxVehicleDriveTankSmoothDigitalRawInputsAndSetAnalogInputs
(const PxVehicleKeySmoothingData& padSmoothing, 
 const PxVehicleDriveTankRawInputData& rawInputData, 
 const PxReal timestep, 
 PxVehicleDriveTank& focusVehicle);


/**
\brief Used to smooth and set analog tank control values from analog inputs (gamepad).
Also used to set boolean gearup, geardown values.
*/
void PxVehicleDriveTankSmoothAnalogRawInputsAndSetAnalogInputs
(const PxVehiclePadSmoothingData& padSmoothing, 
 const PxVehicleDriveTankRawInputData& rawInputData, 
 const PxReal timestep, 
 PxVehicleDriveTank& focusVehicle);


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_CONTROL_H

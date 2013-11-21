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

#ifndef PX_VEHICLE_CORE_COMPONENTS_H
#define PX_VEHICLE_CORE_COMPONENTS_H
/** \addtogroup vehicle
  @{
*/

#include "foundation/PxVec3.h"
#include "common/PxCoreUtilityTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVehicleChassisData
{
public:

	friend class PxVehicleDriveSimData4W;

	PxVehicleChassisData()
		:	mMOI(PxVec3(0,0,0)),
			mMass(1500),
			mCMOffset(PxVec3(0,0,0))
	{
	}

	/**
	\brief Moment of inertia of vehicle rigid body actor
	*/
	PxVec3 mMOI;

	/**
	\brief Mass of vehicle rigid body actor
	*/
	PxReal mMass;

	/**
	\brief Center of mass offset of vehicle rigid body actor
	*/
	PxVec3 mCMOffset;

private:

	PxReal pad;

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleChassisData)& 0x0f));

class PxVehicleEngineData
{
public:

	friend class PxVehicleDriveSimData;

	enum
	{
		eMAX_NUM_ENGINE_TORQUE_CURVE_ENTRIES = 8
	};

	PxVehicleEngineData()
		: 	mPeakTorque(500.0f),
			mMaxOmega(600.0f),
			mDampingRateFullThrottle(0.15f),
			mDampingRateZeroThrottleClutchEngaged(2.0f),
			mDampingRateZeroThrottleClutchDisengaged(0.35f)
	{
		mTorqueCurve.addPair(0.0f, 0.8f);
		mTorqueCurve.addPair(0.33f, 1.0f);
		mTorqueCurve.addPair(1.0f, 0.8f);

		mRecipMaxOmega=1.0f/mMaxOmega;
	}

	/**
	\brief Graph of normalised torque (torque/maxTorque) against normalised engine revs (revs/maxRevs).
	*/
	PxFixedSizeLookupTable<eMAX_NUM_ENGINE_TORQUE_CURVE_ENTRIES> mTorqueCurve;

	/**
	\brief Maximum torque available to apply to the engine, specified in Nm.
	\brief Please note that to optimise the implementation the engine has a hard-coded inertia of 1kgm^2.
	\brief As a consequence the magnitude of the engine's angular acceleration is exactly equal to the magnitude of the torque driving the engine.
	\brief To simulate engines with different inertias (!=1kgm^2) adjust either the entries of mTorqueCurve or mPeakTorque accordingly.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mPeakTorque;

	/**
	\brief Maximum rotation speed of the engine, specified in radians per second.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxOmega;

	/**
	\brief Damping rate of engine in s^-1 when full throttle is applied.
	Damping rate applied at run-time is an interpolation between mDampingRateZeroThrottleClutchEngaged and mDampingRateFullThrottle
	if the clutch is engaged.  If the clutch is disengaged (in neutral gear) the damping rate applied at run-time is an interpolation
	between mDampingRateZeroThrottleClutchDisengaged and mDampingRateFullThrottle.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mDampingRateFullThrottle;

	/**
	\brief Damping rate of engine in s^-1 at zero throttle when the clutch is engaged.
	Damping rate applied at run-time is an interpolation between mDampingRateZeroThrottleClutchEngaged and mDampingRateFullThrottle
	if the clutch is engaged.  If the clutch is disengaged (in neutral gear) the damping rate applied at run-time is an interpolation
	between mDampingRateZeroThrottleClutchDisengaged and mDampingRateFullThrottle.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mDampingRateZeroThrottleClutchEngaged;

	/**
	\brief Damping rate of engine in s^-1 at zero throttle when the clutch is disengaged (in neutral gear).
	Damping rate applied at run-time is an interpolation between mDampingRateZeroThrottleClutchEngaged and mDampingRateFullThrottle
	if the clutch is engaged.  If the clutch is disengaged (in neutral gear) the damping rate applied at run-time is an interpolation
	between mDampingRateZeroThrottleClutchDisengaged and mDampingRateFullThrottle.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mDampingRateZeroThrottleClutchDisengaged;

	/**
	\brief Return value of mRecipMaxOmega(=1.0f/mMaxOmega) that is automatically set by PxVehicleDriveSimData::setEngineData
	*/
	PX_FORCE_INLINE PxReal getRecipMaxOmega() const {return mRecipMaxOmega;}

private:

	/**
	\brief Reciprocal of the maximum rotation speed of the engine.
	Not necessary to set this value because it is set by PxVehicleDriveSimData::setEngineData
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRecipMaxOmega;

	PxReal mPad[2];

	bool isValid() const;

};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleEngineData)& 0x0f));

class PxVehicleGearsData
{
public:

	friend class PxVehicleDriveSimData;

	enum
	{
		eREVERSE=0,
		eNEUTRAL,
		eFIRST,
		eSECOND,
		eTHIRD,
		eFOURTH,
		eFIFTH,
		eSIXTH,
		eSEVENTH,
		eEIGHTH,
		eNINTH,
		eTENTH,
		eELEVENTH,
		eTWELFTH,
		eTHIRTEENTH,
		eFOURTEENTH,
		eFIFTEENTH,
		eSIXTEENTH,
		eSEVENTEENTH,
		eEIGHTEENTH,
		eNINETEENTH,
		eTWENTIETH,
		eTWENTYFIRST,
		eTWENTYSECOND,
		eTWENTYTHIRD,
		eTWENTYFOURTH,
		eTWENTYFIFTH,
		eTWENTYSIXTH,
		eTWENTYSEVENTH,
		eTWENTYEIGHTH,
		eTWENTYNINTH,
		eTHIRTIETH,
		eMAX_NUM_GEAR_RATIOS
	};

	PxVehicleGearsData()
		: 	mFinalRatio(4.0f),
			mNumRatios(7),
			mSwitchTime(0.5f)
	{
		mRatios[PxVehicleGearsData::eREVERSE]=-4.0f;
		mRatios[PxVehicleGearsData::eNEUTRAL]=0.0f;
		mRatios[PxVehicleGearsData::eFIRST]=4.0f;
		mRatios[PxVehicleGearsData::eSECOND]=2.0f;
		mRatios[PxVehicleGearsData::eTHIRD]=1.5f;
		mRatios[PxVehicleGearsData::eFOURTH]=1.1f;
		mRatios[PxVehicleGearsData::eFIFTH]=1.0f;
	}

	/**
	\brief Gear ratios 
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRatios[eMAX_NUM_GEAR_RATIOS];

	/**
	\brief Gear ratio applied is mRatios[currentGear]*finalRatio
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mFinalRatio;

	/**
	\brief Number of gears (including reverse and neutral).
	<b>Range:</b> (0,MAX_NUM_GEAR_RATIOS)<br>
	*/
	PxU32 mNumRatios;
	
	/**
	\brief Time it takes to switch gear, specified in s.
	<b>Range:</b> (0,MAX_NUM_GEAR_RATIOS)<br>
	*/
	PxReal mSwitchTime;

private:

	PxReal mPad;

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleGearsData)& 0x0f));

class PxVehicleAutoBoxData
{
public:

	friend class PxVehicleDriveSimData;

	PxVehicleAutoBoxData()
	{
		for(PxU32 i=0;i<PxVehicleGearsData::eMAX_NUM_GEAR_RATIOS;i++)
		{
			mUpRatios[i]=0.65f;
			mDownRatios[i]=0.50f;
		}
		//Not sure how important this is but we want to kick out of neutral very quickly.
		mUpRatios[PxVehicleGearsData::eNEUTRAL]=0.15f;
		//Set the latency time in an unused element of one of the arrays.
		mDownRatios[PxVehicleGearsData::eREVERSE]=2.0f;	
	}
	
	/**
	\brief Value of engineRevs/maxEngineRevs that is high enough to increment gear.
	<b>Range:</b> (0,1)<br>
	*/
	PxReal mUpRatios[PxVehicleGearsData::eMAX_NUM_GEAR_RATIOS];

	/**
	\brief Value of engineRevs/maxEngineRevs that is low enough to decrement gear.
	<b>Range:</b> (0,1)<br>
	*/
	PxReal mDownRatios[PxVehicleGearsData::eMAX_NUM_GEAR_RATIOS];

	/**
	\brief Set the latency time of the autobox, specified in s.
	\brief Latency time is the minimum time that must pass between each gear change that is initiated by the autobox.
	@see getLatency
	*/
	void setLatency(const PxReal latency) 
	{ 
		mDownRatios[PxVehicleGearsData::eREVERSE]=latency;
	}

	/**
	\brief Get the latency time of the autobox, specified in s.
	@see getLatency
	*/
	PxReal getLatency() const 
	{ 
		return mDownRatios[PxVehicleGearsData::eREVERSE];
	}

private:

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleAutoBoxData)& 0x0f));

class PxVehicleDifferential4WData
{
public:

	friend class PxVehicleDriveSimData4W;

	enum
	{
		eDIFF_TYPE_LS_4WD,			//limited slip differential for car with 4 driven wheels
		eDIFF_TYPE_LS_FRONTWD,		//limited slip differential for car with front-wheel drive
		eDIFF_TYPE_LS_REARWD,		//limited slip differential for car with rear-wheel drive
		eDIFF_TYPE_OPEN_4WD,		//open differential for car with 4 driven wheels 
		eDIFF_TYPE_OPEN_FRONTWD,	//open differential for car with front-wheel drive
		eDIFF_TYPE_OPEN_REARWD,		//open differentila for car with rear-wheel drive
		eMAX_NUM_DIFF_TYPES
	};

	PxVehicleDifferential4WData()
		:	mFrontRearSplit(0.45f),
			mFrontLeftRightSplit(0.5f),
			mRearLeftRightSplit(0.5f),
			mCentreBias(1.3f),
			mFrontBias(1.3f),
			mRearBias(1.3f),
			mType(PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD)
	{
	}

	/**
	\brief Ratio of torque split between front and rear (>0.5 means more to front, <0.5 means more to rear).
	\brief Only applied to DIFF_TYPE_LS_4WD and eDIFF_TYPE_OPEN_4WD
	<b>Range:</b> (0,1)<br>
	*/
	PxReal mFrontRearSplit;

	/**
	\brief Ratio of torque split between front-left and front-right (>0.5 means more to front-left, <0.5 means more to front-right).
	\brief Only applied to DIFF_TYPE_LS_4WD and eDIFF_TYPE_OPEN_4WD and eDIFF_TYPE_LS_FRONTWD
	<b>Range:</b> (0,1)<br>
	*/
	PxReal mFrontLeftRightSplit;

	/**
	\brief Ratio of torque split between rear-left and rear-right (>0.5 means more to rear-left, <0.5 means more to rear-right).
	\brief Only applied to DIFF_TYPE_LS_4WD and eDIFF_TYPE_OPEN_4WD and eDIFF_TYPE_LS_REARWD
	<b>Range:</b> (0,1)<br>
	*/
	PxReal mRearLeftRightSplit;

	/**
	\brief Maximum allowed ratio of average front wheel rotation speed and rear wheel rotation speeds 
	\brief Only applied to DIFF_TYPE_LS_4WD
	<b>Range:</b> (1,inf)<br>
	*/
	PxReal mCentreBias;

	/**
	\brief Maximum allowed ratio of front-left and front-right wheel rotation speeds.
	\brief Only applied to DIFF_TYPE_LS_4WD and DIFF_TYPE_LS_FRONTWD
	<b>Range:</b> (1,inf)<br>
	*/
	PxReal mFrontBias;

	/**
	\brief Maximum allowed ratio of rear-left and rear-right wheel rotation speeds.
	\brief Only applied to DIFF_TYPE_LS_4WD and DIFF_TYPE_LS_REARWD
	<b>Range:</b> (1,inf)<br>
	*/
	PxReal mRearBias;

	/**
	\brief Type of differential.
	<b>Range:</b> (DIFF_TYPE_LS_4WD,DIFF_TYPE_OPEN_FRONTWD)<br>
	*/
	PxU32 mType;

private:

	PxReal mPad[1];

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleDifferential4WData)& 0x0f));

class PxVehicleAckermannGeometryData
{
public:

	friend class PxVehicleDriveSimData4W;

	PxVehicleAckermannGeometryData()
		: 	mAccuracy(1.0f),
			mFrontWidth(0.0f),		//Must be filled out 
			mRearWidth(0.0f),		//Must be filled out
			mAxleSeparation(0.0f)	//Must be filled out
	{
	}

	/**
	\brief Accuracy of Ackermann steer calculation.
	\brief Accuracy with value 0.0f results in no Ackermann steer-correction
	\brief Accuracy with value 1.0 results in perfect Ackermann steer-correction.
	<b>Range:</b> (0,1)<br>
	*/		
	PxReal mAccuracy;

	/**
	\brief Distance between center-point of the two front wheels, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mFrontWidth;		

	/**
	\brief Distance between center-point of the two rear wheels, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRearWidth;		

	/**
	\brief Distance between center of front axle and center of rear axle, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mAxleSeparation;	

private:

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleAckermannGeometryData)& 0x0f));

class PxVehicleClutchData
{
public:

	friend class PxVehicleDriveSimData;

	PxVehicleClutchData()
		: 	mStrength(10.0f)
	{
	}

	/**
	\brief Strength of clutch.  
	\brief Torque generated by clutch is proportional to the clutch strength and 
	\brief the velocity difference between the engine speed and the speed of the driven wheels 
	\brief after accounting for the gear ratio.
	<b>Range:</b> (0,MAX_NUM_GEAR_RATIOS)<br>
	*/
	PxReal mStrength;

private:

	PxReal mPad[3];

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleClutchData)& 0x0f));


/**
\brief Tire load can be strongly dependent on the timestep so it is a good idea to filter it 
to give less jerky handling behavior.  The filtered tire load is used as an input to the tire model.
\brief Two points on graph with normalised tire load on x-axis and filtered normalised tire load on y-axis.
\brief Loads less than mMinNormalisedLoad have filtered normalised load = zero.
\brief Loads greater than mMaxNormalisedLoad have filtered normalised load = mMaxFilteredNormalisedLoad.
\brief Loads in-between are linearly interpolated between 0 and mMaxFilteredNormalisedLoad.
\brief The two graphs points that we specify are (mMinNormalisedLoad,0) and (mMaxNormalisedLoad,mMaxFilteredNormalisedLoad).
*/
class PxVehicleTireLoadFilterData
{
public:

	friend class PxVehicleWheelsSimData;

	PxVehicleTireLoadFilterData()
		: 	mMinNormalisedLoad(-0.25f),
			mMaxNormalisedLoad(3.0f),
			mMaxFilteredNormalisedLoad(3.0f)
	{
		mDenominator=1.0f/(mMaxNormalisedLoad - mMinNormalisedLoad);
	}

	/**
	\brief Graph point (mMinNormalisedLoad,0)
	*/
	PxReal mMinNormalisedLoad; 

	/**
	\brief Graph point (mMaxNormalisedLoad,mMaxFilteredNormalisedLoad)
	*/
	PxReal mMaxNormalisedLoad;
		
	/**
	\brief Graph point (mMaxNormalisedLoad,mMaxFilteredNormalisedLoad)
	*/
	PxReal mMaxFilteredNormalisedLoad;

	PX_FORCE_INLINE PxReal getDenominator() const {return mDenominator;}

private:

	/**
	\brief Not necessary to set this value.
	*/
	//1.0f/(mMaxNormalisedLoad-mMinNormalisedLoad) for quick calculations
	PxReal mDenominator;

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleTireLoadFilterData)& 0x0f));

class PxVehicleWheelData
{
public:

	friend class PxVehicleWheels4SimData;

	PxVehicleWheelData()
		: 	mRadius(0.0f),				//Must be filled out
			mWidth(0.0f),
			mMass(20.0f),
			mMOI(0.0f),					//Must be filled out
			mDampingRate(0.25f),
			mMaxBrakeTorque(1500.0f),
			mMaxHandBrakeTorque(0.0f),	
			mMaxSteer(0.0f),			
			mToeAngle(0.0f),
			mRecipRadius(0.0f),			//Must be filled out
			mRecipMOI(0.0f)				//Must be filled out
	{
	}

	/**
	\brief Radius of unit that includes metal wheel plus rubber tire, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRadius;

	/**
	\brief Maximum width of unit that includes wheel plus tire, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mWidth;

	/**
	\brief Mass of unit that includes wheel plus tire, specified in kg.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMass;

	/**
	\brief Moment of inertia of unit that includes wheel plus tire about single allowed axis of rotation, specified in kg m^2.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMOI;

	/**
	\brief Damping rate applied to wheel.
	*/
	PxReal mDampingRate;

	/**
	\brief Max brake torque that can be applied to wheel, specified in Nm.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxBrakeTorque;

	/**
	\brief Max handbrake torque that can be applied to wheel, specified in Nm
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxHandBrakeTorque;

	/**
	\brief Max steer angle that can be achieved by the wheel, specified in radians.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxSteer;

	/**
	\brief Wheel toe angle, specified in radians.
	<b>Range:</b> (0,Pi/2)<br>
	*/
	PxReal mToeAngle;//in radians

	/**
	\brief Return value equal to 1.0f/mRadius
	@see PxVehicleWheelsSimData::setWheelData
	*/
	PX_FORCE_INLINE PxReal getRecipRadius() const {return mRecipRadius;}


	/**
	\brief Return value equal to 1.0f/mRecipMOI
	@see PxVehicleWheelsSimData::setWheelData
	*/
	PX_FORCE_INLINE PxReal getRecipMOI() const {return mRecipMOI;}

private:

	/**
	\brief Reciprocal of radius of unit that includes metal wheel plus rubber tire.
	Not necessary to set this value because it is set by PxVehicleWheelsSimData::setWheelData
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRecipRadius;

	/**
	\brief Reciprocal of moment of inertia of unit that includes wheel plus tire about single allowed axis of rotation.
	Not necessary to set this value because it is set by PxVehicleWheelsSimData::setWheelData
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mRecipMOI;

	PxReal mPad[1];

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleWheelData)& 0x0f));

class PxVehicleSuspensionData
{
public:

	friend class PxVehicleWheels4SimData;

	PxVehicleSuspensionData()
		: 	mSpringStrength(0.0f),
			mSpringDamperRate(0.0f),
			mMaxCompression(0.3f),
			mMaxDroop(0.1f),
			mSprungMass(0.0f)
	{
	}

	/**
	\brief Spring strength of suspension unit, specified in N m^-1.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mSpringStrength;

	/**
	\brief Spring damper rate of suspension unit, specified in s^-1.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mSpringDamperRate;

	/**
	\brief Maximum compression allowed by suspension spring, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxCompression;

	/**
	\brief Maximum elongation allowed by suspension spring, specified in m.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mMaxDroop;

	/**
	\brief Mass of vehicle that is supported by suspension spring, specified in kg.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mSprungMass;

private:

	PxReal mPad[3];

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleSuspensionData)& 0x0f));

class PxVehicleTireData
{
public:

	friend class PxVehicleWheels4SimData;

	PxVehicleTireData()
		: 	mLatStiffX(2.0f),
			mLatStiffY(0.3125f*(180.0f / PxPi)),
			mLongitudinalStiffnessPerUnitGravity(1000.0f),
			mCamberStiffness(1.0f*(180.0f / PxPi)),
			mType(0)
	{
		mFrictionVsSlipGraph[0][0]=0.0f;
		mFrictionVsSlipGraph[0][1]=1.0f;
		mFrictionVsSlipGraph[1][0]=0.1f;
		mFrictionVsSlipGraph[1][1]=1.0f;
		mFrictionVsSlipGraph[2][0]=1.0f;
		mFrictionVsSlipGraph[2][1]=1.0f;

		mRecipLongitudinalStiffnessPerUnitGravity=1.0f/mLongitudinalStiffnessPerUnitGravity;
		mFrictionVsSlipGraphRecipx1Minusx0=1.0f/(mFrictionVsSlipGraph[1][0]-mFrictionVsSlipGraph[0][0]);
		mFrictionVsSlipGraphRecipx2Minusx1=1.0f/(mFrictionVsSlipGraph[2][0]-mFrictionVsSlipGraph[1][0]);
	}

	/**
	\brief Tire lateral stiffness is typically a graph of tire load that has linear behaviour near zero load and 
	flattens at large loads.  mLatStiffX describes the minimum normalised load (load/restLoad) 
	that gives a flat lateral stiffness response.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mLatStiffX;

	/**
	\brief Tire lateral stiffness is a graph of tire load that has linear behavior near zero load and 
	flattens at large loads.  mLatStiffY describes the maximum possible lateral stiffness 
	divided by the rest tire load, specified in "per radian"
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mLatStiffY;

	/**
	\brief Tire Longitudinal stiffness per unit longitudinal slip per unit gravity, specified in N per radian per unit gravitational acceleration
	Longitudinal stiffness of the tire per unit longitudinal slip is calculated as gravitationalAcceleration*mLongitudinalStiffnessPerUnitGravity
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mLongitudinalStiffnessPerUnitGravity;

	/**
	\brief Camber stiffness, specified in N per radian.
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mCamberStiffness;

	/**
	\brief Graph of friction vs longitudinal slip with 3 points. 
	\brief mFrictionVsSlipGraph[0][0] is always zero.
	\brief mFrictionVsSlipGraph[0][1] is the friction available at zero longitudinal slip.
	\brief mFrictionVsSlipGraph[1][0] is the value of longitudinal slip with maximum friction.
	\brief mFrictionVsSlipGraph[1][1] is the maximum friction.
	\brief mFrictionVsSlipGraph[2][0] is the end point of the graph.
	\brief mFrictionVsSlipGraph[2][1] is the value of friction for slips greater than mFrictionVsSlipGraph[2][0].
	<b>Range:</b> (0,inf)<br>
	*/
	PxReal mFrictionVsSlipGraph[3][2];

	/**
	\brief Tire type denoting slicks, wets, snow, winter, summer, all-terrain, mud etc.
	<b>Range:</b> (0,inf)<br>
	*/
	PxU32 mType;

	/**
	\brief Return Cached value of 1.0/mLongitudinalStiffnessPerUnitGravity
	@see PxVehicleWheelsSimData::setTireData
	*/
	PX_FORCE_INLINE PxReal getRecipLongitudinalStiffnessPerUnitGravity() const {return mRecipLongitudinalStiffnessPerUnitGravity;}

	/**
	\brief Return Cached value of 1.0f/(mFrictionVsSlipGraph[1][0]-mFrictionVsSlipGraph[0][0])
	@see PxVehicleWheelsSimData::setTireData
	*/
	PX_FORCE_INLINE PxReal getFrictionVsSlipGraphRecipx1Minusx0() const {return mFrictionVsSlipGraphRecipx1Minusx0;}

	/**
	\brief Return Cached value of 1.0f/(mFrictionVsSlipGraph[2][0]-mFrictionVsSlipGraph[1][0])
	@see PxVehicleWheelsSimData::setTireData
	*/
	PX_FORCE_INLINE PxReal getFrictionVsSlipGraphRecipx2Minusx1() const {return mFrictionVsSlipGraphRecipx2Minusx1;}

private:

	/**
	\brief Cached value of 1.0/mLongitudinalStiffnessPerUnitGravity.
	\brief Not necessary to set this value because it is set by PxVehicleWheelsSimData::setTireData
	@see PxVehicleWheelsSimData::setTireData
	*/
	PxReal mRecipLongitudinalStiffnessPerUnitGravity;

	/**
	\brief Cached value of 1.0f/(mFrictionVsSlipGraph[1][0]-mFrictionVsSlipGraph[0][0])
	\brief Not necessary to set this value because it is set by PxVehicleWheelsSimData::setTireData
	@see PxVehicleWheelsSimData::setTireData
	*/
	PxReal mFrictionVsSlipGraphRecipx1Minusx0;

	/**
	\brief Cached value of 1.0f/(mFrictionVsSlipGraph[2][0]-mFrictionVsSlipGraph[1][0])
	\brief Not necessary to set this value because it is set by PxVehicleWheelsSimData::setTireData
	@see PxVehicleWheelsSimData::setTireData
	*/
	PxReal mFrictionVsSlipGraphRecipx2Minusx1;

	PxReal mPad[2];

	bool isValid() const;
};
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleTireData)& 0x0f));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_CORE_COMPONENTS_H

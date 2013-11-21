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

#ifndef PX_VEHICLE_UTILSTELEMETRY_H
#define PX_VEHICLE_UTILSTELEMETRY_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleSDK.h"
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

#if PX_DEBUG_VEHICLE_ON

class PxVehicleGraphDesc
{

	friend class PxVehicleGraph;

	PxVehicleGraphDesc();

	/**
	\brief x-coord of graph centre.
	<b>Range:</b> (0,1)<br>
	*/	
	PxReal mPosX;

	/**
	\brief y-coord of graph centre.
	<b>Range:</b> (0,1)<br>
	*/	
	PxReal mPosY;

	/**
	\brief x-extents of graph (from mPosX-0.5f*mSizeX to mPosX+0.5f*mSizeX).
	<b>Range:</b> (0,1)<br>
	*/	
	PxReal mSizeX;

	/**
	\brief y-extents of graph (from mPosY-0.5f*mSizeY to mPosY+0.5f*mSizeY).
	<b>Range:</b> (0,1)<br>
	*/	
	PxReal mSizeY;

	/**
	\brief Background color of graph.
	*/	
	PxVec3 mBackgroundColor;

	/**
	\brief Alpha value of background color.
	*/	
	PxReal mAlpha;

private:

	bool isValid() const;
};

struct PxVehicleGraphChannelDesc
{
public:

	friend class PxVehicleGraph;

	PxVehicleGraphChannelDesc();

	/**
	\brief Data values less than mMinY will be clamped at mMinY.
	*/	
	PxReal mMinY;

	/**
	\brief Data values greater than mMaxY will be clamped at mMaxY.
	*/	
	PxReal mMaxY;

	/**
	\brief Data values greater than mMidY will be drawn with color mColorHigh.
	\brief Data values less than mMidY will be drawn with color mColorLow.
	*/	
	PxReal mMidY;

	/**
	\brief Color used to render data values lower than mMidY.
	*/	
	PxVec3 mColorLow;

	/**
	\brief Color used to render data values greater than mMidY.
	*/	
	PxVec3 mColorHigh;

	/**
	\brief String to describe data channel.
	*/	
	char* mTitle;

private:

	bool isValid() const;
};

class PxVehicleGraph
{
public:

	friend class PxVehicleTelemetryData;
	friend class PxVehicleUpdate;

	enum
	{
		eMAX_NUM_SAMPLES=256
	};

	enum
	{
		eMAX_NUM_TITLE_CHARS=256
	};

	enum
	{
		eCHANNEL_JOUNCE=0,
		eCHANNEL_SUSPFORCE,
		eCHANNEL_TIRELOAD,
		eCHANNEL_NORMALIZED_TIRELOAD,
		eCHANNEL_WHEEL_OMEGA, 
		eCHANNEL_TIRE_FRICTION,
		eCHANNEL_TIRE_LONG_SLIP,
		eCHANNEL_NORM_TIRE_LONG_FORCE,
		eCHANNEL_TIRE_LAT_SLIP,
		eCHANNEL_NORM_TIRE_LAT_FORCE,
		eCHANNEL_NORM_TIRE_ALIGNING_MOMENT,
		eMAX_NUM_WHEEL_CHANNELS
	};

	enum
	{
		eCHANNEL_ENGINE_REVS=0,
		eCHANNEL_ENGINE_DRIVE_TORQUE,
		eCHANNEL_CLUTCH_SLIP,
		eCHANNEL_ACCEL_CONTROL,					//TANK_ACCEL
		eCHANNEL_BRAKE_CONTROL,					//TANK_BRAKE_LEFT
		eCHANNEL_HANDBRAKE_CONTROL,				//TANK_BRAKE_RIGHT
		eCHANNEL_STEER_LEFT_CONTROL,			//TANK_THRUST_LEFT
		eCHANNEL_STEER_RIGHT_CONTROL,			//TANK_THRUST_RIGHT
		eCHANNEL_GEAR_RATIO,
		eMAX_NUM_ENGINE_CHANNELS
	};

	enum
	{
		eMAX_NUM_CHANNELS=12
	};

	enum eGraphType
	{
		eGRAPH_TYPE_WHEEL=0,
		eGRAPH_TYPE_ENGINE
	};

	/**
	\brief Setup a graph from a descriptor.
	*/
	void setup(const PxVehicleGraphDesc& desc, const eGraphType graphType);

	/**
	\brief Clear all data recorded in a graph.
	*/
	void clearRecordedChannelData();

	/**
	\brief Get the color of the graph background.  Used for rendering a graph.
	*/
	const PxVec3& getBackgroundColor() const {return mBackgroundColor;}

	/**
	\brief Get the alpha transparency of the color of the graph background.  Used for rendering a graph.
	*/
	PxReal getBackgroundAlpha() const {return mBackgroundAlpha;}

	/**
	\brief Get the coordinates of the graph background.  Used for rendering a graph
	*/	
	void getBackgroundCoords(PxReal& xMin, PxReal& yMin, PxReal& xMax, PxReal& yMax) const {xMin = mBackgroundMinX;xMax = mBackgroundMaxX;yMin = mBackgroundMinY;yMax = mBackgroundMaxY;}

	/**
	\brief Compute the coordinates of the graph data of a specific graph channel.
	\brief xy is the graph data stored in order x0,y0,x1,y1,x2,y2...xn,yn.
	\brief colors stores the color of each point on the graph.
	\brief title is the title of the graph.
	*/
	void computeGraphChannel(const PxU32 channel, PxReal* xy, PxVec3* colors, char* title) const;

private:

	//Min and max of each sample.
	PxReal mChannelMinY[eMAX_NUM_CHANNELS];
	PxReal mChannelMaxY[eMAX_NUM_CHANNELS];
	//Discriminate between high and low values with different colors.
	PxReal mChannelMidY[eMAX_NUM_CHANNELS];
	//Different colors for values than midY and less than midY.
	PxVec3 mChannelColorLow[eMAX_NUM_CHANNELS];
	PxVec3 mChannelColorHigh[eMAX_NUM_CHANNELS];
	//Title of graph
	char mChannelTitle[eMAX_NUM_CHANNELS][eMAX_NUM_TITLE_CHARS];
	//Graph data.
	PxReal mChannelSamples[eMAX_NUM_CHANNELS][eMAX_NUM_SAMPLES];

	//Background color,alpha,coords
	PxVec3 mBackgroundColor;
	PxReal mBackgroundAlpha;
	PxReal mBackgroundMinX;
	PxReal mBackgroundMaxX;
	PxReal mBackgroundMinY;
	PxReal mBackgroundMaxY;

	PxU32 mSampleTide;

	PxU32 mNumChannels;

	PxU32 mPad[2];


	void setup
		(const PxF32 graphSizeX, const PxF32 graphSizeY,
		const PxF32 engineGraphPosX, const PxF32 engineGraphPosY,
		const PxF32* const wheelGraphPosX, const PxF32* const wheelGraphPosY,
		const PxVec3& backgroundColor, const PxVec3& lineColorHigh, const PxVec3& lineColorLow);

	void updateTimeSlice(const PxReal* const samples);

	void setChannel(PxVehicleGraphChannelDesc& desc, const PxU32 channel);

	void setupEngineGraph
		(const PxF32 sizeX, const PxF32 sizeY, const PxF32 posX, const PxF32 posY, 
		const PxVec3& backgoundColor, const PxVec3& lineColorHigh, const PxVec3& lineColorLow);

	void setupWheelGraph
		(const PxF32 sizeX, const PxF32 sizeY, const PxF32 posX, const PxF32 posY, 
		const PxVec3& backgoundColor, const PxVec3& lineColorHigh, const PxVec3& lineColorLow);

	PxVehicleGraph();
	~PxVehicleGraph();
};
PX_COMPILE_TIME_ASSERT(PxU32(PxVehicleGraph::eMAX_NUM_CHANNELS) >= PxU32(PxVehicleGraph::eMAX_NUM_WHEEL_CHANNELS) && PxU32(PxVehicleGraph::eMAX_NUM_CHANNELS) >= PxU32(PxVehicleGraph::eMAX_NUM_ENGINE_CHANNELS));
PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleGraph) & 15));

class PxVehicleTelemetryData
{
public:

	friend class PxVehicleUpdate;

	/**
	\brief Allocate a PxVehicleNWTelemetryData instance for a vehicle with 4*numSuspWheelTire4 wheels.
	@see PxVehicleNWTelemetryDataFree
	*/
	static PxVehicleTelemetryData* allocate(const PxU32 numWheels);

	/**
	\brief Free a PxVehicleNWTelemetryData instance for a vehicle.
	@see PxVehicleNWTelemetryDataAllocate
	*/
	void free();

	/**
	\brief Set up all the graphs so that they are ready to record data.
	*/
	void setup
		(const PxReal graphSizeX, const PxReal graphSizeY,
		 const PxReal engineGraphPosX, const PxReal engineGraphPosY,
		 const PxReal* const wheelGraphPosX, const PxReal* const wheelGraphPosY,
		 const PxVec3& backGroundColor, const PxVec3& lineColorHigh, const PxVec3& lineColorLow);

	/**
	\brief Clear the graphs of recorded data.
	*/
	void clear();

	/**
	\brief Get the graph data for the engine
	*/
	const PxVehicleGraph& getEngineGraph() const {return *mEngineGraph;}

	/**
	\brief Get the number of wheel graphs
	*/
	PxU32 getNumWheelGraphs() const {return mNumActiveWheels;}

	/**
	\brief Get the graph data for the kth wheel
	*/
	const PxVehicleGraph& getWheelGraph(const PxU32 k) const {return mWheelGraphs[k];}

	/**
	\brief Get the array of tire force application points so they can be rendered
	*/
	const PxVec3* getTireforceAppPoints() const {return mTireforceAppPoints;}

	/**
	\brief Get the array of susp force application points so they can be rendered
	*/
	const PxVec3* getSuspforceAppPoints() const {return mSuspforceAppPoints;}

private:

	/**
	\brief Graph data for engine.
	\brief Used for storing single timeslices of debug data for engine graph.
	@see PxVehicleGraph
	*/
	PxVehicleGraph* mEngineGraph;

	/**
	\brief Graph data for each wheel.
	\brief Used for storing single timeslices of debug data for wheel graphs.
	@see PxVehicleGraph
	*/
	PxVehicleGraph* mWheelGraphs;

	/**
	\brief Application point of tire forces.
	*/
	PxVec3* mTireforceAppPoints;

	/**
	\brief Application point of susp forces.
	*/
	PxVec3* mSuspforceAppPoints;

	/**
	\brief Total number of active wheels 
	*/
	PxU32 mNumActiveWheels;

	PxU32 mPad[3];

private:

	PxVehicleTelemetryData(){}
	~PxVehicleTelemetryData(){}
};

PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleTelemetryData) & 15));

#endif //PX_DEBUG_VEHICLE_ON

//#endif // PX_DEBUG_VEHICLE_ON

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_UTILSTELEMETRY_H

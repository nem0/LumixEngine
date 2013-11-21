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

#ifndef PX_VEHICLE_SHADERS_H
#define PX_VEHICLE_SHADERS_H
/** \addtogroup vehicle
  @{
*/

#include "vehicle/PxVehicleSDK.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Prototype of shader function that computes wheel torque and tire forces.
\brief Default implementation is PxVehicleComputeTireForceDefault.
\brief shaderData - shader data for the tire being processed (see PxVehicleTireForceCalculator).
\brief tireFriction - friction value of the tire contact.
\brief longSlip - longitudinal slip of the tire.
\brief latSlip - lateral slip of the tire.
\brief camber - camber angle of the tire.
\brief wheelOmega - rotational speed of the wheel.
\brief wheelRadius - the distance from the tire surface and the center of the wheel.
\brief recipWheelRadius - the reciprocal of wheelRadius.
\brief restTireLoad - the load force experienced by the tire when the vehicle is at rest.
\brief normalisedTireLoad - a value equal to the load force on the tire divided by the restTireLoad.
\brief tireLoad - the load force currently experienced by the tire.
\brief gravity - magnitude of gravitational acceleration.
\brief recipGravity - the reciprocal of the magnitude of gravitational acceleration.
\brief wheelTorque - the torque to be applied to the wheel around the wheel axle.
\brief tireLongForceMag - the magnitude of the longitudinal tire force to be applied to the vehicle's rigid body.
\brief tireLatForceMag - the magnitude of the lateral tire force to be applied to the vehicle's rigid body.
\brief tireAlignMoment - the aligning moment of the tire that is to be applied to the vehicle's rigid body (not currently used).
@see PxVehicleComputeTireForceDefault
*/
typedef void (*PxVehicleComputeTireForce)
(const void* shaderData, 
 const PxF32 tireFriction,
 const PxF32 longSlip, const PxF32 latSlip, const PxF32 camber,
 const PxF32 wheelOmega, const PxF32 wheelRadius, const PxF32 recipWheelRadius,
 const PxF32 restTireLoad, const PxF32 normalisedTireLoad, const PxF32 tireLoad,
 const PxF32 gravity, const PxF32 recipGravity,
 PxF32& wheelTorque, PxF32& tireLongForceMag, PxF32& tireLatForceMag, PxF32& tireAlignMoment);

/**
\brief Default implementation of PxVehicleComputeTireForce
@see PxVehicleComputeTireForce, PxVehicleTireForceCalculator
*/
void PxVehicleComputeTireForceDefault
 (const void* shaderData, 
 const PxF32 tireFriction,
 const PxF32 longSlip, const PxF32 latSlip, const PxF32 camber,
 const PxF32 wheelOmega, const PxF32 wheelRadius, const PxF32 recipWheelRadius,
 const PxF32 restTireLoad, const PxF32 normalisedTireLoad, const PxF32 tireLoad,
 const PxF32 gravity, const PxF32 recipGravity,
 PxF32& wheelTorque, PxF32& tireLongForceMag, PxF32& tireLatForceMag, PxF32& tireAlignMoment);

/**
\brief Structure containing shader data for each tire of a vehicle and a shader function that computes individual tire forces 
*/
class PxVehicleTireForceCalculator
{
public:

	PxVehicleTireForceCalculator()
		: mShader(PxVehicleComputeTireForceDefault)
	{
	}

	/**
	\brief Array of shader data - one data entry per tire.
	Default values are pointers to PxVehicleTireData (stored in PxVehicleWheelsSimData) and are set in PxVehicleDriveTank::setup or PxVehicleDrive4W::setup
	@see PxVehicleComputeTireForce, PxVehicleComputeTireForceDefault, PxVehicleWheelsSimData, PxVehicleDriveTank::setup, PxVehicleDrive4W::setup
	*/
	const void** mShaderData;

	/**
	\brief Shader function.
	Default value is PxVehicleComputeTireForceDefault and is set in  PxVehicleDriveTank::setup or PxVehicleDrive4W::setup
	@see PxVehicleComputeTireForce, PxVehicleComputeTireForceDefault, PxVehicleWheelsSimData, PxVehicleDriveTank::setup, PxVehicleDrive4W::setup
	*/
	PxVehicleComputeTireForce mShader;

#ifndef PX_X64
	PxU32 mPad[2];
#endif
};

PX_COMPILE_TIME_ASSERT(0==(sizeof(PxVehicleTireForceCalculator) & 15));

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_SHADERS_H

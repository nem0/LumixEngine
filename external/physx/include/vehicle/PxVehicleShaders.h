/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
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
\brief Prototype of shader function that is used to compute wheel torque and tire forces.
\param[in]  shaderData is the shader data for the tire being processed.  The shader data describes the tire data in the format required by the tire model that is implemented by the shader function.
\param[in]  tireFriction is the value of friction for the contact between the tire and the ground.
\param[in]  longSlip is the value of longitudinal slip experienced by the tire.
\param[in]  latSlip is the value of lateral slip experienced by the tire.
\param[in]  camber is the camber angle of the tire in radians.
\param[in]  wheelOmega is the rotational speed of the wheel.
\param[in]  wheelRadius is the distance from the tire surface to the center of the wheel.
\param[in]  recipWheelRadius is the reciprocal of wheelRadius.
\param[in]  restTireLoad is the load force experienced by the tire when the vehicle is at rest.
\param[in]  normalisedTireLoad is a pre-computed value equal to the load force on the tire divided by restTireLoad.
\param[in]  tireLoad is the load force currently experienced by the tire (= restTireLoad*normalisedTireLoad)
\param[in]  gravity is the magnitude of gravitational acceleration.
\param[in]  recipGravity is the reciprocal of the magnitude of gravitational acceleration.
\param[out] wheelTorque is the torque that is to be applied to the wheel around the wheel's axle.
\param[out] tireLongForceMag is the magnitude of the longitudinal tire force to be applied to the vehicle's rigid body.
\param[out] tireLatForceMag is the magnitude of the lateral tire force to be applied to the vehicle's rigid body.
\param[out] tireAlignMoment is the aligning moment of the tire that is to be applied to the vehicle's rigid body (not currently used).
@see PxVehicleWheelsDynData::setTireForceShaderFunction,  PxVehicleWheelsDynData::setTireForceShaderData
*/
typedef void (*PxVehicleComputeTireForce)
(const void* shaderData, 
 const PxF32 tireFriction,
 const PxF32 longSlip, const PxF32 latSlip, const PxF32 camber,
 const PxF32 wheelOmega, const PxF32 wheelRadius, const PxF32 recipWheelRadius,
 const PxF32 restTireLoad, const PxF32 normalisedTireLoad, const PxF32 tireLoad,
 const PxF32 gravity, const PxF32 recipGravity,
 PxF32& wheelTorque, PxF32& tireLongForceMag, PxF32& tireLatForceMag, PxF32& tireAlignMoment);


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif //PX_VEHICLE_SHADERS_H

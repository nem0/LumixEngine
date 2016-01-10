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


#ifndef PX_PHYSICS_CCT_CAPSULE_CONTROLLER
#define PX_PHYSICS_CCT_CAPSULE_CONTROLLER
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"
#include "characterkinematic/PxController.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

struct PxCapsuleClimbingMode
{
	enum Enum
	{
		eEASY,			//!< Standard mode, let the capsule climb over surfaces according to impact normal
		eCONSTRAINED,	//!< Constrained mode, try to limit climbing according to the step offset

		eLAST
	};
};

/**
\brief A descriptor for a capsule character controller.

@see PxCapsuleController PxControllerDesc
*/
class PxCapsuleControllerDesc : public PxControllerDesc
{
public:
	/**
	\brief constructor sets to default.
	*/
	PX_INLINE								PxCapsuleControllerDesc ();
	PX_INLINE virtual						~PxCapsuleControllerDesc ();

	/**
	\brief (re)sets the structure to the default.
	*/
	PX_INLINE virtual	void				setToDefault();
	/**
	\brief returns true if the current settings are valid

	\return True if the descriptor is valid.
	*/
	PX_INLINE virtual	bool				isValid()		const;

	/**
	\brief The radius of the capsule

	<b>Default:</b> 0.0

	@see PxCapsuleController
	*/
	PxF32				radius;

	/**
	\brief The height of the controller

	<b>Default:</b> 0.0

	@see PxCapsuleController
	*/
	PxF32				height;

	/**
	\brief The climbing mode

	<b>Default:</b> PxCapsuleClimbingMode::eEASY

	@see PxCapsuleController
	*/
	PxCapsuleClimbingMode::Enum		climbingMode;
};

PX_INLINE PxCapsuleControllerDesc::PxCapsuleControllerDesc () : PxControllerDesc(PxControllerShapeType::eCAPSULE)
{
	radius = height = 0.0f;
	climbingMode = PxCapsuleClimbingMode::eEASY;
}

PX_INLINE PxCapsuleControllerDesc::~PxCapsuleControllerDesc()
{
}

PX_INLINE void PxCapsuleControllerDesc::setToDefault()
{
	*this = PxCapsuleControllerDesc();
}

PX_INLINE bool PxCapsuleControllerDesc::isValid() const
{
	if(!PxControllerDesc::isValid())	return false;
	if(radius<=0.0f)					return false;
	if(height<=0.0f)					return false;
	if(stepOffset>height+radius*2.0f)	return false;	// Prevents obvious mistakes
	return true;
}
/**
\brief A capsule character controller.

The capsule is defined as a position, a vertical height, and a radius.
The height is the distance between the two sphere centers at the end of the capsule.
In other words:

p = pos (returned by controller)<br>
h = height<br>
r = radius<br>

p = center of capsule<br>
top sphere center = p.y + h*0.5<br>
bottom sphere center = p.y - h*0.5<br>
top capsule point = p.y + h*0.5 + r<br>
bottom capsule point = p.y - h*0.5 - r<br>
*/
class PxCapsuleController : public PxController
{
protected:
	PX_INLINE					PxCapsuleController()	{}
	virtual						~PxCapsuleController()	{}

public:

	/**
	\brief Gets controller's radius.

	\return The radius of the controller.

	@see PxCapsuleControllerDesc.radius setRadius()
	*/
	virtual		PxF32			getRadius() const = 0;

	/**
	\brief Sets controller's radius.

	\warning this doesn't check for collisions.

	\param[in] radius The new radius for the controller.
	\return Currently always true.

	@see PxCapsuleControllerDesc.radius getRadius()
	*/
	virtual		bool			setRadius(PxF32 radius) = 0;

	/**
	\brief Gets controller's height.

	\return The height of the capsule controller.

	@see PxCapsuleControllerDesc.height setHeight()
	*/
	virtual		PxF32			getHeight() const = 0;

	/**
	\brief Resets controller's height.

	\warning this doesn't check for collisions.

	\param[in] height The new height for the controller.
	\return Currently always true.

	@see PxCapsuleControllerDesc.height getHeight()
	*/
	virtual		bool			setHeight(PxF32 height) = 0;

	/**
	\brief Gets controller's climbing mode.

	\return The capsule controller's climbing mode.

	@see PxCapsuleControllerDesc.climbingMode setClimbingMode()
	*/
	virtual		PxCapsuleClimbingMode::Enum		getClimbingMode()	const	= 0;

	/**
	\brief Sets controller's climbing mode.

	\param[in] mode The capsule controller's climbing mode.

	@see PxCapsuleControllerDesc.climbingMode getClimbingMode()
	*/
	virtual		bool			setClimbingMode(PxCapsuleClimbingMode::Enum mode)	= 0;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

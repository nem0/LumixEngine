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


#ifndef PX_PHYSICS_CCT_BOX_CONTROLLER
#define PX_PHYSICS_CCT_BOX_CONTROLLER
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"
#include "characterkinematic/PxController.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Descriptor for a box character controller.

@see PxBoxController PxControllerDesc
*/
class PxBoxControllerDesc : public PxControllerDesc
{
public:
	/**
	\brief constructor sets to default.
	*/
	PX_INLINE								PxBoxControllerDesc();
	PX_INLINE virtual						~PxBoxControllerDesc();

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
	\brief Half height

	<b>Default:</b> 1.0
	*/
	PxF32				halfHeight;			// Half-height in the "up" direction

	/**
	\brief Half side extent

	<b>Default:</b> 0.5
	*/
	PxF32				halfSideExtent;		// Half-extent in the "side" direction

	/**
	\brief Half forward extent

	<b>Default:</b> 0.5
	*/
	PxF32				halfForwardExtent;	// Half-extent in the "forward" direction
};

PX_INLINE PxBoxControllerDesc::PxBoxControllerDesc() :
	PxControllerDesc	(PxControllerShapeType::eBOX),
	halfHeight			(1.0f),
	halfSideExtent		(0.5f),
	halfForwardExtent	(0.5f)
{
}

PX_INLINE PxBoxControllerDesc::~PxBoxControllerDesc()
{
}

PX_INLINE void PxBoxControllerDesc::setToDefault()
{
	*this = PxBoxControllerDesc();
}

PX_INLINE bool PxBoxControllerDesc::isValid() const
{
	if(!PxControllerDesc::isValid())	return false;
	if(halfHeight<=0.0f)				return false;
	if(halfSideExtent<=0.0f)			return false;
	if(halfForwardExtent<=0.0f)			return false;
	if(stepOffset>2.0f*halfHeight)		return false;	// Prevents obvious mistakes
	return true;
}

/**
\brief Box character controller.

@see PxBoxControllerDesc PxController
*/
class PxBoxController : public PxController
{
protected:
	PX_INLINE					PxBoxController()	{}
	virtual						~PxBoxController()	{}

public:

	/**
	\brief Gets controller's half height.

	\return The half height of the controller.

	@see PxBoxControllerDesc.halfHeight setHalfHeight()
	*/
	virtual		PxF32			getHalfHeight()			const	= 0;

	/**
	\brief Gets controller's half side extent.

	\return The half side extent of the controller.

	@see PxBoxControllerDesc.halfSideExtent setHalfSideExtent()
	*/
	virtual		PxF32			getHalfSideExtent()		const	= 0;

	/**
	\brief Gets controller's half forward extent.

	\return The half forward extent of the controller.

	@see PxBoxControllerDesc.halfForwardExtent setHalfForwardExtent()
	*/
	virtual		PxF32			getHalfForwardExtent()	const	= 0;

	/**
	\brief Sets controller's half height.

	\warning this doesn't check for collisions.

	\param[in] halfHeight The new half height for the controller.
	\return Currently always true.

	@see PxBoxControllerDesc.halfHeight getHalfHeight()
	*/
	virtual		bool			setHalfHeight(PxF32 halfHeight)					= 0;

	/**
	\brief Sets controller's half side extent.

	\warning this doesn't check for collisions.

	\param[in] halfSideExtent The new half side extent for the controller.
	\return Currently always true.

	@see PxBoxControllerDesc.halfSideExtent getHalfSideExtent()
	*/
	virtual		bool			setHalfSideExtent(PxF32 halfSideExtent)			= 0;

	/**
	\brief Sets controller's half forward extent.

	\warning this doesn't check for collisions.

	\param[in] halfForwardExtent The new half forward extent for the controller.
	\return Currently always true.

	@see PxBoxControllerDesc.halfForwardExtent getHalfForwardExtent()
	*/
	virtual		bool			setHalfForwardExtent(PxF32 halfForwardExtent)	= 0;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

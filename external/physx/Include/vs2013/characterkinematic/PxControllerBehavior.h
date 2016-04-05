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

#ifndef PX_PHYSICS_CCT_BEHAVIOR
#define PX_PHYSICS_CCT_BEHAVIOR
/** \addtogroup character
  @{
*/

#include "PxFiltering.h"
#include "characterkinematic/PxCharacter.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxShape;
	class PxObstacle;
	class PxController;

	/**
	\brief specifies controller behavior
	*/
	struct PxControllerBehaviorFlag
	{
		enum Enum
		{
			eCCT_CAN_RIDE_ON_OBJECT		= (1<<0),	//!< Controller can ride on touched object (i.e. when this touched object is moving horizontally). \note The CCT vs. CCT case is not supported.
			eCCT_SLIDE					= (1<<1),	//!< Controller should slide on touched object
			eCCT_USER_DEFINED_RIDE		= (1<<2)	//!< Disable all code dealing with controllers riding on objects, let users define it outside of the SDK.
		};
	};

	/**
	\brief Bitfield that contains a set of raised flags defined in PxControllerBehaviorFlag.

	@see PxControllerBehaviorFlag
	*/
	typedef PxFlags<PxControllerBehaviorFlag::Enum, PxU8> PxControllerBehaviorFlags;
	PX_FLAGS_OPERATORS(PxControllerBehaviorFlag::Enum, PxU8)

	/**
	\brief User behavior callback.

	This behavior callback is called to customize the controller's behavior w.r.t. touched shapes.
	*/
	class PxControllerBehaviorCallback
	{
	public:
		//*********************************************************************
		// DEPRECATED FUNCTIONS:
		//
		//	PX_DEPRECATED virtual PxU32 getBehaviorFlags(const PxShape& shape) = 0;
		//
		//	=> replaced with:
		//
		//	virtual PxControllerBehaviorFlags getBehaviorFlags(const PxShape& shape, const PxActor& actor) = 0;
		//
		// ----------------------------
		//
		//	PX_DEPRECATED virtual PxU32 getBehaviorFlags(const PxController& controller) = 0;
		//
		//	=> replaced with:
		//
		//	virtual PxControllerBehaviorFlags getBehaviorFlags(const PxController& controller) = 0;
		//
		// ----------------------------
		//
		//	PX_DEPRECATED virtual PxU32 getBehaviorFlags(const PxObstacle& obstacle) = 0;
		//
		//	=> replaced with:
		//
		//	virtual PxControllerBehaviorFlags getBehaviorFlags(const PxObstacle& obstacle) = 0;
		//
		//*********************************************************************

		/**
		\brief Retrieve behavior flags for a shape.

		When the CCT touches a shape, the CCT's behavior w.r.t. this shape can be customized by users.
		This function retrieves the desired PxControllerBehaviorFlag flags capturing the desired behavior.

		\note See comments about deprecated functions at the start of this class

		\param[in] shape	The shape the CCT is currently touching
		\param[in] actor	The actor owning the shape

		\return Desired behavior flags for the given shape

		@see PxControllerBehaviorFlag
		*/
		virtual PxControllerBehaviorFlags getBehaviorFlags(const PxShape& shape, const PxActor& actor) = 0;

		/**
		\brief Retrieve behavior flags for a controller.

		When the CCT touches a controller, the CCT's behavior w.r.t. this controller can be customized by users.
		This function retrieves the desired PxControllerBehaviorFlag flags capturing the desired behavior.

		\note The flag PxControllerBehaviorFlag::eCCT_CAN_RIDE_ON_OBJECT is not supported.
		\note See comments about deprecated functions at the start of this class

		\param[in] controller	The controller the CCT is currently touching

		\return Desired behavior flags for the given controller

		@see PxControllerBehaviorFlag
		*/
		virtual PxControllerBehaviorFlags getBehaviorFlags(const PxController& controller) = 0;

		/**
		\brief Retrieve behavior flags for an obstacle.

		When the CCT touches an obstacle, the CCT's behavior w.r.t. this obstacle can be customized by users.
		This function retrieves the desired PxControllerBehaviorFlag flags capturing the desired behavior.

		\note See comments about deprecated functions at the start of this class

		\param[in] obstacle		The obstacle the CCT is currently touching

		\return Desired behavior flags for the given obstacle

		@see PxControllerBehaviorFlag
		*/
		virtual PxControllerBehaviorFlags getBehaviorFlags(const PxObstacle& obstacle) = 0;

	protected:
		virtual ~PxControllerBehaviorCallback(){}
	};

#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif

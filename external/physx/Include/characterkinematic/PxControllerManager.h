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


#ifndef PX_PHYSICS_CCT_MANAGER
#define PX_PHYSICS_CCT_MANAGER
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"

#include "PxPhysX.h"
#include "common/PxRenderBuffer.h"
#include "foundation/PxFoundation.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPhysics;
class PxScene;
class PxController;
class PxControllerDesc;
class PxControllerManager;
class PxObstacleContext;

/**
\brief specifies debug-rendering flags
*/
struct PxControllerDebugRenderFlags
{
	enum Enum
	{
		eTEMPORAL_BV	= (1<<0),	//!< Temporal bounding volume around controllers
		eCACHED_BV		= (1<<1),	//!< Cached bounding volume around controllers
		eOBSTACLES		= (1<<2),	//!< User-defined obstacles

		eALL			= 0xffffffff
	};
};

/**
\brief Manages an array of character controllers.

@see PxController PxBoxController PxCapsuleController
*/
class PX_PHYSX_CHARACTER_API PxControllerManager {
public:
	/**
	\brief Releases the controller manager.
	*/
	virtual void				release() = 0;

	/**
	\brief Returns the number of controllers that are being managed.

	\return The number of controllers.
	*/
	virtual PxU32				getNbControllers() const = 0;

	/**
	\brief Retrieve one of the controllers in the manager.

	\param index the index of the controller to return
	\return an array of controller pointers with size getNbControllers().
	*/
	virtual PxController*		getController(PxU32 index) = 0;

	/**
	\brief Creates a new character controller.

	\param[in] sdk The Physics sdk object
	\param[in] scene The scene that the controller will belong to.
	\param[in] desc The controllers descriptor
	\return The new controller

	@see PxController PxController.release() PxControllerDesc
	*/
	virtual PxController*		createController(PxPhysics& sdk, PxScene* scene, const PxControllerDesc& desc) = 0;

	/**
	\brief Releases all the controllers that are being managed.
	*/
	virtual void				purgeControllers() = 0;

	/**
	\brief Retrieves debug data.

	\return The render buffer filled with debug-render data

	@see PxControllerManager.setDebugRenderingFlags()
	*/
	virtual	PxRenderBuffer&		getRenderBuffer()		= 0;

	/**
	\brief Sets debug rendering flags

	\param[in] flags The debug rendering flags (combination of PxControllerDebugRenderFlags)

	@see PxControllerManager.getRenderBuffer() PxControllerDebugRenderFlags
	*/
	virtual	void				setDebugRenderingFlags(PxU32 flags)	= 0;

	/**
	\brief Creates an obstacle context.

	\return New obstacle context

	@see PxObstacleContext
	*/
	virtual	PxObstacleContext*	createObstacleContext()	= 0;

	/**
	\brief Computes character-character interactions.

	This function is an optional helper to properly resolve interactions between characters, in case they overlap (which can happen for gameplay reasons, etc).

	You should call this once per frame, before your PxController::move() calls. The function will not move the characters directly, but it will
	compute overlap information for each character that will be used in the next move() call.
	
	You need to provide a proper time value here so that interactions are resolved in a way that do not depend on the framerate.

	If you only have one character in the scene, or if you can guarantee your characters will never overlap, then you do not need to call this function.

	\param[in] elapsedTime	Elapsed time since last call
	*/
	virtual	void				computeInteractions(PxF32 elapsedTime) = 0;

protected:
	PxControllerManager() {}
	virtual ~PxControllerManager() {}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

PX_C_EXPORT PX_PHYSX_CHARACTER_API physx::PxControllerManager* PX_CALL_CONV PxCreateControllerManager(physx::PxFoundation& foundation);

/** @} */
#endif //PX_PHYSICS_CCT_MANAGER

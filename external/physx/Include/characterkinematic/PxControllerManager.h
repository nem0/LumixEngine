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


#ifndef PX_PHYSICS_CCT_MANAGER
#define PX_PHYSICS_CCT_MANAGER
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"

#include "PxPhysXConfig.h"
#include "foundation/PxFlags.h"
#include "common/PxRenderBuffer.h"
#include "foundation/PxFoundation.h"
#include "foundation/PxErrorCallback.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxPhysics;
class PxScene;
class PxController;
class PxControllerDesc;
class PxObstacleContext;
class PxControllerFilterCallback;

/**
\brief specifies debug-rendering flags
*/
struct PxControllerDebugRenderFlag
{
	enum Enum
	{
		eTEMPORAL_BV	= (1<<0),	//!< Temporal bounding volume around controllers
		eCACHED_BV		= (1<<1),	//!< Cached bounding volume around controllers
		eOBSTACLES		= (1<<2),	//!< User-defined obstacles

		eNONE			= 0,
		eALL			= 0xffffffff
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxControllerDebugRenderFlag.

@see PxControllerDebugRenderFlag
*/
typedef PxFlags<PxControllerDebugRenderFlag::Enum, PxU32> PxControllerDebugRenderFlags;
PX_FLAGS_OPERATORS(PxControllerDebugRenderFlag::Enum, PxU32)


/**
\brief Manages an array of character controllers.

@see PxController PxBoxController PxCapsuleController
*/
class PX_PHYSX_CHARACTER_API PxControllerManager
{
public:
	/**
	\brief Releases the controller manager.

	\note This will release all associated controllers and obstacle contexts.

	\note This function is required to be called to release foundation usage.

	*/
	virtual void				release() = 0;

	/**
	\brief Returns the scene the manager is adding the controllers to.

	\return The associated physics scene.
	*/
	virtual PxScene&			getScene() const = 0;

	/**
	\brief Returns the number of controllers that are being managed.

	\return The number of controllers.
	*/
	virtual PxU32				getNbControllers() const = 0;

	/**
	\brief Retrieve one of the controllers in the manager.

	\param index the index of the controller to return
	\return The controller with the specified index.
	*/
	virtual PxController*		getController(PxU32 index) = 0;

	/**
	\brief Creates a new character controller.

	\param[in] desc The controllers descriptor
	\return The new controller

	@see PxController PxController.release() PxControllerDesc
	*/
	virtual PxController*		createController(const PxControllerDesc& desc) = 0;

	/** \deprecated */
	PX_DEPRECATED virtual PxController*		createController(PxPhysics&, PxScene*, const PxControllerDesc& desc)
	{
		// The SDK & scene parameters are not needed anymore
		PxGetFoundation().getErrorCallback().reportError(PxErrorCode::eDEBUG_WARNING, "The PxControllerManager::createController prototype has changed. Please upate your code.", __FILE__, __LINE__);
		return createController(desc);
	}

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
	virtual	void				setDebugRenderingFlags(PxControllerDebugRenderFlags flags)	= 0;

	/**
	\brief Returns the number of obstacle contexts that are being managed.

	\return The number of obstacle contexts.
	*/
	virtual PxU32				getNbObstacleContexts() const = 0;

	/**
	\brief Retrieve one of the obstacle contexts in the manager.

	\param index The index of the obstacle context to retrieve.
	\return The obstacle context with the specified index.
	*/
	virtual PxObstacleContext*	getObstacleContext(PxU32 index) = 0;

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

	\note Releasing the manager will automatically release all the associated obstacle contexts.

	\param[in] elapsedTime	Elapsed time since last call
	\param[in] cctFilterCb	Filtering callback for CCT-vs-CCT interactions
	*/
	virtual	void				computeInteractions(PxF32 elapsedTime, PxControllerFilterCallback* cctFilterCb=NULL) = 0;

	/**
	\brief Enables or disables runtime tessellation.

	Large triangles can create accuracy issues in the sweep code, which in turn can lead to characters not sliding smoothly
	against geometries, or even penetrating them. This feature allows one to reduce those issues by tessellating large
	triangles at runtime, before performing sweeps against them. The amount of tessellation is controlled by the 'maxEdgeLength' parameter.
	Any triangle with at least one edge length greater than the maxEdgeLength will get recursively tessellated, until resulting triangles are small enough.

	This features only applies to triangle meshes, convex meshes, heightfields and boxes.

	\param[in] flag				True/false to enable/disable runtime tessellation.
	\param[in] maxEdgeLength	Max edge length allowed before tessellation kicks in.
	*/
	virtual	void				setTessellation(bool flag, float maxEdgeLength) = 0;

	/**
	\brief Enables or disables the overlap recovery module.

	The overlap recovery module can be used to depenetrate CCTs from static objects when an overlap is detected. This can happen
	in three main cases:
		- when the CCT is directly spawned or teleported in another object
		- when the CCT algorithm fails due to limited FPU accuracy
		- when the "up vector" is modified, making the rotated CCT shape overlap surrounding objects

	When activated, the CCT module will automatically try to resolve the penetration, and move the CCT to a safe place where it does
	not overlap other objects anymore. This only concerns static objects, dynamic objects are ignored by the recovery module.

	When the recovery module is not activated, it is possible for the CCTs to go through static objects. By default, the recovery
	module is enabled.

	The recovery module currently works with all geometries except heightfields.

	\param[in] flag				True/false to enable/disable overlap recovery module.
	*/
	virtual	void				setOverlapRecoveryModule(bool flag) = 0;

	/**
	\brief Enables or disables the precise sweeps.

	Precise sweeps are more accurate, but also potentially slower than regular sweeps.

	By default, precise sweeps are enabled.

	\param[in] flag				True/false to enable/disable precise sweeps.
	*/
	virtual	void				setPreciseSweeps(bool flag) = 0;

	/**
	\brief Enables or disables vertical sliding against ceilings.

	Geometry is seen as "ceilings" when the following condition is met:

		dot product(contact normal, up direction)<0.0f

	This flag controls whether characters should slide vertically along the geometry in that case.

	By default, sliding is allowed.

	\param[in] flag				True/false to enable/disable sliding.
	*/
	virtual	void				setPreventVerticalSlidingAgainstCeiling(bool flag) = 0;

	/**
	\brief Shift the origin of the character controllers and obstacle objects by the specified vector.

	The positions of all character controllers, obstacle objects and the corresponding data structures will get adjusted to reflect the shifted origin location
	(the shift vector will get subtracted from all character controller and obstacle object positions).

	\note It is the user's responsibility to keep track of the summed total origin shift and adjust all input/output to/from PhysXCharacterKinematic accordingly.

	\note This call will not automatically shift the PhysX scene and its objects. You need to call PxScene::shiftOrigin() seperately to keep the systems in sync.

	\param[in] shift Translation vector to shift the origin by.
	*/
	virtual	void				shiftOrigin(const PxVec3& shift) = 0;

protected:
	PxControllerManager() {}
	virtual ~PxControllerManager() {}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

PX_C_EXPORT PX_PHYSX_CHARACTER_API physx::PxControllerManager* PX_CALL_CONV PxCreateControllerManager(physx::PxScene& scene);

/** @} */
#endif //PX_PHYSICS_CCT_MANAGER

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



#ifndef PX_PHYSICS_CCT_CONTROLLER
#define PX_PHYSICS_CCT_CONTROLLER
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"
#include "characterkinematic/PxExtended.h"
#include "PxSceneQueryFiltering.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief The type of controller, eg box, sphere or capsule.
*/
struct PxControllerShapeType
{
	enum Enum
	{
		/**
		\brief A box controller.

		@see PxBoxController PxBoxControllerDesc
		*/
		eBOX,

		/**
		\brief A capsule controller

		@see PxCapsuleController PxCapsuleControllerDesc
		*/
		eCAPSULE,

		eFORCE_DWORD = 0x7fffffff
	};
};

class PxShape;
class PxScene;
class PxController;
class PxRigidDynamic;
class PxMaterial;
struct PxFilterData;
class PxSceneQueryFilterCallback;
class PxControllerBehaviorCallback;
class PxObstacleContext;
class PxObstacle;

/**
\brief specifies how a CCT interacts with other CCTs.

This member controls if a character controller will collide with another controller. There are 3 options:
always collide, never collide and collide based on the shape group.
This flag only affects other controllers when they move; when this controller moves, the flag is ignored
and the flags of the other controllers determine collision.
*/
struct PxCCTInteractionMode
{
	enum Enum
	{
		eINCLUDE,		//!< Always collide character controllers.
		eEXCLUDE,		//!< Never collide character controllers.

		/**
		\brief Collide based on a group bitmask stored in the controller.

		The groups to collide against are passed in the activeGroups member of #PxController::move(). The active
		groups flags work on top of the Physics SDK filtering logic of the controllers kinematic actor to determine if a 
		collision should occur:

		activeGroups & controller->getGroupsBitmask()

		@see PxController.move() PxController.getGroupsBitmask() PxController.setGroupsBitmask()
		*/
		eUSE_FILTER,	
	};
};

/**
\brief specifies how a CCT interacts with non-walkable parts.

This is only used when slopeLimit is non zero.

*/
struct PxCCTNonWalkableMode
{
	enum Enum
	{
		ePREVENT_CLIMBING,	//!< Stops character from climbing up a slope, but doesn't move it otherwise
		eFORCE_SLIDING,		//!< Forces character to slide down non-walkable slopes
	};
};

/**
\brief specifies which sides a character is colliding with.
*/
struct PxControllerFlag
{
	enum Enum
	{
		eCOLLISION_SIDES	= (1<<0),	//!< Character is colliding to the sides.
		eCOLLISION_UP		= (1<<1),	//!< Character has collision above.
		eCOLLISION_DOWN		= (1<<2),	//!< Character has collision below.
	};
};

/**
\brief Describes a controller's internal state.
*/
struct PxControllerState
{
	PxVec3			deltaXP;
	PxShape*		touchedShape;		// Shape on which the CCT is standing
	PxObstacle*		touchedObstacle;	// Obstacle on which the CCT is standing
	PxU32			collisionFlags;		// Last known collision flags (PxControllerFlag)
	bool			standOnAnotherCCT;	// Are we standing on another CCT?
	bool			standOnObstacle;	// Are we standing on a user-defined obstacle?
	bool			isMovingUp;			// is CCT moving up or not? (i.e. explicit jumping)
};

/**
\brief Describes a controller's internal statistics.
*/
struct PxControllerStats
{
	PxU16			nbIterations;
	PxU16			nbFullUpdates;
	PxU16			nbPartialUpdates;
};

/**
\brief Describes a generic CCT hit.
*/
struct PxCCTHit
{
	PxController*	controller;		//!< Current controller
	PxExtendedVec3	worldPos;		//!< Contact position in world space
	PxVec3			worldNormal;	//!< Contact normal in world space
	PxVec3			dir;			//!< Motion direction
	PxF32			length;			//!< Motion length
};

/**
\brief Describes a hit between a CCT and a shape. Passed to onShapeHit()

@see PxUserControllerHitReport.onShapeHit()
*/
struct PxControllerShapeHit : PxCCTHit
{
	PxShape*		shape;			//!< Touched shape
	PxU32			triangleIndex;	//!< touched triangle index (only for meshes/heightfields)
};

/**
\brief Describes a hit between a CCT and another CCT. Passed to onControllerHit().

@see PxUserControllerHitReport.onControllerHit()
*/
struct PxControllersHit : PxCCTHit
{
	PxController*	other;			//!< Touched controller
};

/**
\brief Describes a hit between a CCT and a user-defined obstacle. Passed to onObstacleHit().

@see PxUserControllerHitReport.onObstacleHit() PxObstacleContext
*/
struct PxControllerObstacleHit : PxCCTHit
{
	const void*		userData;
};

/**
\brief User callback class for character controller events.

\note Character controller hit reports are only generated when move is called.

@see PxControllerDesc.callback
*/
class PxUserControllerHitReport
{
public:

	/**
	\brief Called when current controller hits a shape.

	\param[in] hit Provides information about the hit.

	@see PxControllerShapeHit
	*/
	virtual void onShapeHit(const PxControllerShapeHit& hit) = 0;

	/**
	\brief Called when current controller hits another controller.

	\param[in] hit Provides information about the hit.

	@see PxControllersHit
	*/
	virtual void onControllerHit(const PxControllersHit& hit) = 0;

	/**
	\brief Called when current controller hits a user-defined obstacle.

	\param[in] hit Provides information about the hit.

	@see PxControllerObstacleHit PxObstacleContext
	*/
	virtual void onObstacleHit(const PxControllerObstacleHit& hit) = 0;

protected:
	virtual ~PxUserControllerHitReport(){}
};


/**
\brief Filtering data for "move" call

@see PxController.move()
*/
class PxControllerFilters
{
	public:
	PX_INLINE					PxControllerFilters(PxU32 groups=0xffffffff, const PxFilterData* filterData=NULL, PxSceneQueryFilterCallback* cb=NULL) :
									mActiveGroups	(groups),
									mFilterData		(filterData),
									mFilterCallback	(cb),
									mFilterFlags	(PxSceneQueryFilterFlag::eSTATIC|PxSceneQueryFilterFlag::eDYNAMIC|PxSceneQueryFilterFlag::ePREFILTER)
								{}

	PxU32						mActiveGroups;			//!< a filtering mask for collision groups. If a bit is set, corresponding group is active.
	const PxFilterData*			mFilterData;			//!< alternative filter data used to filter shapes
	PxSceneQueryFilterCallback*	mFilterCallback;		//!< custom filter logic to filter out colliding objects.
	PxSceneQueryFilterFlags		mFilterFlags;			//!< filter flags
};


/**
\brief Descriptor class for a character controller.

@see PxBoxController PxCapsuleController
*/
class PxControllerDesc
{
protected:

	PxControllerShapeType::Enum type;		//!< The type of the controller. This gets set by the derived class' ctor, the user should not have to change it.

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE										PxControllerDesc(PxControllerShapeType::Enum);
	PX_INLINE virtual								~PxControllerDesc();
public:

	/**
	\brief returns true if the current settings are valid

	\return True if the descriptor is valid.
	*/
	PX_INLINE virtual	bool						isValid()		const;

	/**
	\brief Returns the character controller type

	\return The controllers type.

	@see PxControllerType PxCapsuleControllerDesc PxBoxControllerDesc
	*/
	PX_INLINE			PxControllerShapeType::Enum		getType()		const	{ return type;		}

	/**
	\brief The position of the character

	<b>Default:</b> Zero
	*/
	PxExtendedVec3				position;

	/**
	\brief Specifies the 'up' direction

	In order to provide stepping functionality the SDK must be informed about the up direction.

	<b>Default:</b> (0, 1, 0)

	*/
	PxVec3						upDirection;

	/**
	\brief The maximum slope which the character can walk up.

	In general it is desirable to limit where the character can walk, in particular it is unrealistic
	for the character to be able to climb arbitary slopes.

	The limit is expressed as the cosine of desired limit angle. A value of 0 disables this feature.

	<b>Default:</b> 0.707

	@see upDirection invisibleWallHeight maxJumpHeight
	*/
	PxF32						slopeLimit;

	/**
	\brief Height of invisible walls created around non-walkable triangles

	The library can automatically create invisible walls around non-walkable triangles defined
	by the 'slopeLimit' parameter. This defines the height of those walls. If it is 0.0, then
	no extra triangles are created.

	<b>Default:</b> 0.0

	@see upDirection slopeLimit maxJumpHeight
	*/
	PxF32						invisibleWallHeight;

	/**
	\brief Maximum height a jumping character can reach

	This is only used if invisible walls are created ('invisibleWallHeight' is non zero).

	When a character jumps, the non-walkable triangles he might fly over are not found
	by the collision queries (since the character's bounding volume does not touch them).
	Thus those non-walkable triangles do not create invisible walls, and it is possible
	for a jumping character to land on a non-walkable triangle, while he wouldn't have
	reached that place by just walking.

	The 'maxJumpHeight' variable is used to extend the size of the collision volume
	downward. This way, all the non-walkable triangles are properly found by the collision
	queries and it becomes impossible to 'jump over' invisible walls.

	If the character in your game can not jump, it is safe to use 0.0 here. Otherwise it
	is best to keep this value as small as possible, since a larger collision volume
	means more triangles to process.

	<b>Default:</b> 0.0

	@see upDirection slopeLimit invisibleWallHeight
	*/
	PxF32						maxJumpHeight;

	/**
	\brief The contact offset used by the controller.

	Specifies a skin around the object within which contacts will be generated.
	Use it to avoid numerical precision issues.

	This is dependant on the scale of the users world, but should be a small, positive 
	non zero value.

	<b>Default:</b> 0.1
	*/
	PxF32						contactOffset;

	/**
	\brief Defines the maximum height of an obstacle which the character can climb.

	A small value will mean that the character gets stuck and cannot walk up stairs etc, 
	a value which is too large will mean that the character can climb over unrealistically 
	high obstacles.

	<b>Default:</b> 0.5

	@see upDirection 
	*/
	PxF32						stepOffset;

	/**
	\brief Density of underlying kinematic actor

	The CCT creates a PhysX's kinematic actor under the hood. This controls its density.

	<b>Default:</b> 10.0
	*/
	PxF32						density;

	/**
	\brief Scale coeff for underlying kinematic actor

	The CCT creates a PhysX's kinematic actor under the hood. This controls its scale factor.
	This should be a number a bit smaller than 1.0.

	<b>Default:</b> 0.8
	*/
	PxF32						scaleCoeff;

	/**
	\brief Cached volume growth

	Amount of space around the controller we cache to improve performance. This is a scale factor
	that should be higher than 1.0f but not too big, ideally lower than 2.0f.

	<b>Default:</b> 1.5
	*/
	PxF32						volumeGrowth;

	/**
	\brief Specifies a user report callback.

	This report callback is called when the character collides with shapes and other characters.

	Setting this to NULL disables the callback.

	<b>Default:</b> NULL

	@see PxUserControllerHitReport
	*/
	PxUserControllerHitReport*	callback;

	/**
	\brief Specifies a user behavior callback.

	This behavior callback is called to customize the controller's behavior w.r.t. touched shapes.

	Setting this to NULL disables the callback.

	<b>Default:</b> NULL

	@see PxControllerBehaviorCallback
	*/
	PxControllerBehaviorCallback*	behaviorCallback;

	/**
	\brief The interaction mode controls if a character controller collides with other controllers.

	The default is to collide controllers.

	<b>Default:</b> PxCCTInteractionMode::eINCLUDE

	@see PxCCTInteractionMode
	*/
	PxCCTInteractionMode::Enum	interactionMode;

	/**
	\brief The non-walkable mode controls if a character controller slides or not on a non-walkable part.

	This is only used when slopeLimit is non zero.

	<b>Default:</b> PxCCTNonWalkableMode::ePREVENT_CLIMBING

	@see PxCCTNonWalkableMode
	*/
	PxCCTNonWalkableMode::Enum	nonWalkableMode;

	/**
	\brief The group bitmasks defines collision filtering when PxCCTInteractionMode::eUSE_FILTER is used.

	<b>Default:</b> 0xffffffff

	@see PxCCTInteractionMode
	*/
	PxU32						groupsBitmask;

	/**
	\brief The material for the actor associated with the controller.
	
	The controller internally creates a rigid body actor. This parameter specifies the material of the actor.

	<b>Default:</b> NULL

	@see PxMaterial
	*/
	PxMaterial*					material;

	/**
	\brief User specified data associated with the controller.

	<b>Default:</b> NULL
	*/
	void*						userData;
};

PX_INLINE PxControllerDesc::PxControllerDesc(PxControllerShapeType::Enum t) : type(t)
{
	upDirection			= PxVec3(0.0f, 1.0f, 0.0f);
	slopeLimit			= 0.707f;
	contactOffset		= 0.1f;
	stepOffset			= 0.5f;
	density				= 10.0f;
	scaleCoeff			= 0.8f;
	volumeGrowth		= 1.5f;
	callback			= NULL;
	behaviorCallback	= NULL;
	userData			= NULL;
	interactionMode		= PxCCTInteractionMode::eINCLUDE;
	nonWalkableMode		= PxCCTNonWalkableMode::ePREVENT_CLIMBING;
	groupsBitmask		= 0xffffffff;
	position.x			= PxExtended(0.0);
	position.y			= PxExtended(0.0);
	position.z			= PxExtended(0.0);
	material			= NULL;
	invisibleWallHeight	= 0.0f;
	maxJumpHeight		= 0.0f;
}

PX_INLINE PxControllerDesc::~PxControllerDesc()
{
}

PX_INLINE bool PxControllerDesc::isValid() const
{
	if(scaleCoeff<0.0f)		return false;
	if(volumeGrowth<1.0f)	return false;
	if(density<0.0f)		return false;
	if(slopeLimit<0.0f)		return false;
	if(stepOffset<0.0f)		return false;
	if(contactOffset<0.0f)	return false;
	if(!material)			return false;
	return true;
}


/**
\brief Base class for character controllers.

@see PxCapsuleController PxBoxController
*/
class PxController
{
protected:
	PX_INLINE							PxController()					{}
	virtual								~PxController()					{}

public:

	/**
	\brief Return the type of controller

	@see PxControllerType
	*/
	virtual		PxControllerShapeType::Enum	getType()						= 0;

	/**
	\brief Releases the controller.
	*/
	virtual		void					release() = 0;

	/**
	\brief Moves the character using a "collide-and-slide" algorithm.

	\param[in] disp	Displacement vector
	\param[in] minDist The minimum travelled distance to consider. If travelled distance is smaller, the character doesn't move. 
	This is used to stop the recursive motion algorithm when remaining distance to travel is small.
	\param[in] elapsedTime Time elapsed since last call
	\param[in] filters User-defined filters for this move
	\param[in] obstacles Potential additional obstacles the CCT should collide with.
	\return Collision flags, collection of ::PxControllerFlag
	*/
	virtual		PxU32					move(const PxVec3& disp, PxF32 minDist, PxF32 elapsedTime, const PxControllerFilters& filters, const PxObstacleContext* obstacles=NULL) = 0;

	/**
	\brief Resets controller's position.

	\warning this is a 'teleport' function, it doesn't check for collisions.

	To move the character under normal conditions use the #move() function.

	\param[in] position The new positon for the controller.
	\return Currently always returns true.

	@see PxControllerDesc.position getPosition() getFootPosition() move()
	*/
	virtual		bool					setPosition(const PxExtendedVec3& position) = 0;

	/**
	\brief Retrieve the raw position of the controller.

	The position is updated by calls to move(). Calling this method without calling
	move() will return the last position or the initial position of the controller.

	\return The controllers position

	@see PxControllerDesc.position setPositon() getFootPosition() move()
	*/
	virtual		const PxExtendedVec3&	getPosition()			const	= 0;

	/**
	\brief Retrieve the "foot" position of the controller, i.e. the position of the bottom of the CCT's shape.

	\return The controllers foot position

	@see PxControllerDesc.position setPositon() getPosition() move()
	*/
	virtual		PxExtendedVec3			getFootPosition()		const	= 0;

	/**
	\brief Get the rigid body actor associated with this controller (see PhysX documentation). 
	The behavior upon manually altering this actor is undefined, you should primarily 
	use it for reading const properties.

	\return the actor associated with the controller.
	*/
	virtual		PxRigidDynamic*			getActor()				const	= 0;

	/**
	\brief The step height.

	\param[in] offset The new step offset for the controller.

	@see PxControllerDesc.stepOffset
	*/
	virtual	    void					setStepOffset(const PxF32 offset) =0;

	/**
	\brief Retrieve the step height.

	\return The step offset for the controller.

	@see setStepOffset()
	*/
	virtual	    PxF32					getStepOffset()						const		=0;

	/**
	\brief Sets the interaction mode for the CCT.

	\param[in] flag The new value of the interaction mode.

	\see PxCCTInteractionMode
	*/
	virtual		void					setInteraction(PxCCTInteractionMode::Enum flag)	= 0;

	/**
	\brief Retrieves the interaction mode for the CCT.

	\return The current interaction mode.

	\see PxCCTInteractionMode
	*/
	virtual		PxCCTInteractionMode::Enum	getInteraction()				const		= 0;

	/**
	\brief Sets the non-walkable mode for the CCT.

	\param[in] flag The new value of the non-walkable mode.

	\see PxCCTNonWalkableMode
	*/
	virtual		void						setNonWalkableMode(PxCCTNonWalkableMode::Enum flag)	= 0;

	/**
	\brief Retrieves the non-walkable mode for the CCT.

	\return The current non-walkable mode.

	\see PxCCTNonWalkableMode
	*/
	virtual		PxCCTNonWalkableMode::Enum	getNonWalkableMode()				const		= 0;

	/**
	\brief Sets the groups bitmask.

	\param[in] bitmask The new groups bitmask value

	\see PxCCTInteractionMode
	*/
	virtual		void					setGroupsBitmask(PxU32 bitmask)	= 0;

	/**
	\brief Retrieves the groups bitmask

	\return The current groups bitmask

	\see PxCCTInteractionMode
	*/
	virtual		PxU32					getGroupsBitmask()				const		= 0;

	/**
	\brief Retrieve the contact offset.

	\return The contact offset for the controller.

	@see PxControllerDesc.contactOffset
	*/
	virtual	    PxF32					getContactOffset()					const		=0;

	/**
	\brief Retrieve the 'up' direction.

	\return The up direction for the controller.

	@see PxControllerDesc.upDirection
	*/
	virtual		PxVec3					getUpDirection()					const		=0;

	/**
	\brief Sets the 'up' direction.

	\param[in] up The up direction for the controller.

	@see PxControllerDesc.upDirection
	*/
	virtual		void					setUpDirection(const PxVec3& up)				=0;

	/**
	\brief Retrieve the slope limit.

	\return The slope limit for the controller.

	@see PxControllerDesc.slopeLimit
	*/
	virtual	    PxF32					getSlopeLimit()						const		=0;

	/**
	\brief The character controller uses caching in order to speed up collision testing, this caching can not detect when objects have changed in the scene. You need to call this method when such changes have been made.
	*/
	virtual		void					reportSceneChanged()			= 0;

	/**
	\brief Retrieve the scene associated with the controller.

	\return The physics scene
	*/
	virtual		PxScene*				getScene()						= 0;

	/**
	\brief Returns the user data associated with this controller.

	\return The user pointer associated with the controller.

	@see PxControllerDesc.userData
	*/
	virtual		void*					getUserData()		const		= 0;

	/**
	\brief Returns information about the controller's internal state.

	\param[out] state The controller's internal state

	@see PxControllerState
	*/
	virtual		void					getState(PxControllerState& state)	const		= 0;

	/**
	\brief Returns the controller's internal statistics.

	\param[out] stats The controller's internal statistics

	@see PxControllerStats
	*/
	virtual		void					getStats(PxControllerStats& stats)	const		= 0;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

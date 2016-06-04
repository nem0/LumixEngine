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


#ifndef PX_PHYSICS_NX_RIGIDACTOR
#define PX_PHYSICS_NX_RIGIDACTOR
/** \addtogroup physics
@{
*/

#include "PxActor.h"
#include "PxShape.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxConstraint;
class PxMaterial;
class PxGeometry;

/**
\brief PxRigidActor represents a base class shared between dynamic and static rigid bodies in the physics SDK.

PxRigidActor objects specify the geometry of the object by defining a set of attached shapes (see #PxShape, #createShape()).

@see PxActor
*/

class PxRigidActor : public PxActor
{
public:
	/**
	\brief Deletes the rigid actor object.
	
	Also releases any shapes associated with the actor.

	Releasing an actor will affect any objects that are connected to the actor (constraint shaders like joints etc.).
	Such connected objects will be deleted upon scene deletion, or explicitly by the user by calling release()
	on these objects. It is recommended to always remove all objects that reference actors before the actors
	themselves are removed. It is not possible to retrieve list of dead connected objects.

	<b>Sleeping:</b> This call will awaken any sleeping actors contacting the deleted actor (directly or indirectly).

	Calls #PxActor::release() so you might want to check the documentation of that method as well.

	@see PxActor::release()
	*/
	virtual		void			release() = 0;


/************************************************************************************************/
/** @name Global Pose Manipulation
*/

	/**
	\brief Retrieves the actors world space transform.

	The getGlobalPose() method retrieves the actor's current actor space to world space transformation.

	\return Global pose of object.

	@see PxRigidDynamic.setGlobalPose() PxRigidStatic.setGlobalPose()
	*/
	virtual		PxTransform 	getGlobalPose()		const = 0;

	/**
	\brief Method for setting an actor's pose in the world.

	This method instantaneously changes the actor space to world space transformation. 

	This method is mainly for dynamic rigid bodies (see #PxRigidDynamic). Calling this method on static actors is 
	likely to result in a performance penalty, since internal optimization structures for static actors may need to be 
	recomputed. In addition, moving static actors will not interact correctly with dynamic actors or joints. 
	
	To directly control an actor's position and have it correctly interact with dynamic bodies and joints, create a dynamic 
	body with the PxRigidBodyFlag::eKINEMATIC flag, then use the setKinematicTarget() commands to define its path.

	Even when moving dynamic actors, exercise restraint in making use of this method. Where possible, avoid:
	
	\li moving actors into other actors, thus causing overlap (an invalid physical state)
	
	\li moving an actor that is connected by a joint to another away from the other (thus causing joint error)

	<b>Sleeping:</b> This call wakes dynamic actors if they are sleeping and the autowake parameter is true (default).

	\param[in] pose Transformation from the actors local frame to the global frame. <b>Range:</b> rigid body transform.
	\param[in] autowake whether to wake the object if it is dynamic. This parameter has no effect for static or kinematic actors. If true and the current wake counter value is smaller than #PxSceneDesc::wakeCounterResetValue it will get increased to the reset value.

	@see getGlobalPose()
	*/
	virtual		void			setGlobalPose(const PxTransform& pose, bool autowake = true) = 0;


/************************************************************************************************/
/** @name Shapes
*/


	/**
	\brief Creates a new shape with default properties and a list of materials and adds it to the list of shapes of this actor.
	
	This is equivalent to the following

	PxShape* shape(...) = PxGetPhysics().createShape(...);	// reference count is 1
	actor->attachShape(shape);								// increments reference count
	shape->release();										// releases user reference, leaving reference count at 1

	As a consequence, detachShape() will result in the release of the last reference, and the shape will be deleted.

	\note The default shape flags to be set are: eVISUALIZATION, eSIMULATION_SHAPE, eSCENE_QUERY_SHAPE (see #PxShapeFlag).
	Triangle mesh, heightfield or plane geometry shapes configured as eSIMULATION_SHAPE are not supported for 
	non-kinematic PxRigidDynamic instances.

	\note Creating compounds with a very large number of shapes may adversely affect performance and stability.

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] geometry	the geometry of the shape
	\param[in] materials a pointer to an array of material pointers
	\param[in] materialCount the count of materials
	\param[in] shapeFlags optional PxShapeFlags

	\return The newly created shape.

	@see PxShape PxShape::release()
	*/

	virtual		PxShape*		createShape(const PxGeometry& geometry, PxMaterial*const* materials, PxU16 materialCount, PxShapeFlags shapeFlags = PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE) = 0;

	/**
	\deprecated
	\brief Deprecated function to create shapes with an initial transform. 
	
	Use this instead:
	PxShape* shape = actor->createShape(geometry, materials, materialCount);
	if (shape)
		shape->setLocalPose(transform);

	@see PxShape, createShape(const PxGeometry& geometry, PxMaterial*const* materials, PxU16 materialCount, PxShapeFlags shapeFlags = PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE)
	*/
	PX_DEPRECATED PX_INLINE	PxShape* createShape(const PxGeometry& geometry, PxMaterial*const* materials, PxU32 materialCount, const PxTransform& localPose)
	{
		PxShape* shape = createShape(geometry, materials, PxU16(materialCount));
		if (shape)
			shape->setLocalPose(localPose);
		return shape;
	}

	/**
	\brief Creates a new shape with default properties and a single material adds it to the list of shapes of this actor.

	This is equivalent to the following

	PxShape* shape(...) = PxGetPhysics().createShape(...);	// reference count is 1
	actor->attachShape(shape);								// increments reference count
	shape->release();										// releases user reference, leaving reference count at 1

	As a consequence, detachShape() will result in the release of the last reference, and the shape will be deleted.

	\note The default shape flags to be set are: eVISUALIZATION, eSIMULATION_SHAPE, eSCENE_QUERY_SHAPE (see #PxShapeFlag).
	Triangle mesh, heightfield or plane geometry shapes configured as eSIMULATION_SHAPE are not supported for 
	non-kinematic PxRigidDynamic instances.

	\note Creating compounds with a very large number of shapes may adversely affect performance and stability.

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] geometry	the geometry of the shape
	\param[in] material	the material for the shape
	\param[in] shapeFlags optional PxShapeFlags

	\return The newly created shape.

	@see PxShape PxShape::release() detachShape()
	*/

	PX_FORCE_INLINE	PxShape*	createShape(const PxGeometry& geometry, const PxMaterial& material, PxShapeFlags shapeFlags = PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE)
	{
		PxMaterial* materialPtr = const_cast<PxMaterial*>(&material);
		return createShape(geometry, &materialPtr, 1, shapeFlags);
	}

	/**
	\deprecated
	\brief Deprecated function to create shapes with an initial transform. 
	
	Use this instead:
	PxShape* shape = actor->createShape(geometry, material);
	if (shape)
		shape->setLocalPose(transform);

	@see PxShape, createShape(const PxGeometry& geometry, const PxMaterial& material, PxShapeFlags shapeFlags = PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE)
	*/
	PX_DEPRECATED PX_INLINE	PxShape* createShape(const PxGeometry& geometry, const PxMaterial& material, const PxTransform& localPose)
	{
		PxShape* shape = createShape(geometry, material);
		if (shape)
			shape->setLocalPose(localPose);
		return shape;
	}


	/** attach a shared shape to an actor 

	This call will increment the reference count of the shape.

	\note Mass properties of dynamic rigid actors will not automatically be recomputed 
	to reflect the new mass distribution implied by the shape. Follow this call with a call to 
	the PhysX extensions method #PxRigidBodyExt::updateMassAndInertia() to do that.

	Attaching a triangle mesh, heightfield or plane geometry shape configured as eSIMULATION_SHAPE is not supported for 
	non-kinematic PxRigidDynamic instances.


	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] shape	the shape to attach.

	*/
	virtual void				attachShape(PxShape& shape) = 0;


	/** detach a shape from an actor. 
	
	This will also decrement the reference count of the PxShape, and if the reference count is zero, will cause it to be deleted.

	For static rigid actors it is not possible to detach all shapes associated with the actor.
	An attempt to remove the last shape will be ignored.

	<b>Sleeping:</b> Does <b>NOT</b> wake the actor up automatically.

	\param[in] shape	the shape to detach.
	\param[in] wakeOnLostTouch Specifies whether touching objects from the previous frame should get woken up in the next frame. Only applies to PxArticulation and PxRigidActor types.

	*/
	virtual void				detachShape(PxShape& shape, bool wakeOnLostTouch = true) = 0;


	/**
	\brief Returns the number of shapes assigned to the actor.

	You can use #getShapes() to retrieve the shape pointers.

	\return Number of shapes associated with this actor.

	@see PxShape getShapes()
	*/
	virtual		PxU32			getNbShapes()		const	= 0;


	/**
	\brief Retrieve all the shape pointers belonging to the actor.

	These are the shapes used by the actor for collision detection.

	You can retrieve the number of shape pointers by calling #getNbShapes()

	Note: Removing shapes with #PxShape::release() will invalidate the pointer of the released shape.

	\param[out] userBuffer The buffer to store the shape pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first shape pointer to be retrieved
	\return Number of shape pointers written to the buffer.

	@see PxShape getNbShapes() PxShape::release()
	*/
	virtual		PxU32			getShapes(PxShape** userBuffer, PxU32 bufferSize, PxU32 startIndex=0)			const	= 0;

/************************************************************************************************/
/** @name Constraints
*/

	/**
	\brief Returns the number of constraint shaders attached to the actor.

	You can use #getConstraints() to retrieve the constraint shader pointers.

	\return Number of constraint shaders attached to this actor.

	@see PxConstraint getConstraints()
	*/
	virtual		PxU32			getNbConstraints()		const	= 0;


	/**
	\brief Retrieve all the constraint shader pointers belonging to the actor.

	You can retrieve the number of constraint shader pointers by calling #getNbConstraints()

	Note: Removing constraint shaders with #PxConstraint::release() will invalidate the pointer of the released constraint.

	\param[out] userBuffer The buffer to store the constraint shader pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first constraint pointer to be retrieved
	\return Number of constraint shader pointers written to the buffer.

	@see PxConstraint getNbConstraints() PxConstraint::release()
	*/
	virtual		PxU32			getConstraints(PxConstraint** userBuffer, PxU32 bufferSize, PxU32 startIndex=0)		const	= 0;

protected:
	PX_INLINE					PxRigidActor(PxType concreteType, PxBaseFlags baseFlags) : PxActor(concreteType, baseFlags) {}
	PX_INLINE					PxRigidActor(PxBaseFlags baseFlags) : PxActor(baseFlags) {}
	virtual						~PxRigidActor()	{}
	virtual		bool			isKindOf(const char* name)	const	{	return !strcmp("PxRigidActor", name) || PxActor::isKindOf(name); }
};

PX_DEPRECATED PX_INLINE PxRigidActor*		PxActor::isRigidActor()				{ return is<PxRigidActor>();			}
PX_DEPRECATED PX_INLINE const PxRigidActor*	PxActor::isRigidActor()		const	{ return is<PxRigidActor>();			}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

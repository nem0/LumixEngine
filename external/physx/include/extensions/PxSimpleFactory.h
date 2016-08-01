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


#ifndef PX_PHYSICS_EXTENSIONS_SIMPLE_FACTORY_H
#define PX_PHYSICS_EXTENSIONS_SIMPLE_FACTORY_H
/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxTransform.h"
#include "foundation/PxPlane.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxPhysics;
	class PxMaterial;
	class PxRigidActor;
	class PxRigidDynamic;
	class PxRigidStatic;
	class PxGeometry;
	class PxShape;


/** \brief simple method to create a PxRigidDynamic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the global pose of the new object
	\param[in] geometry the geometry of the new object's shape, which must be a sphere, capsule, box or convex
	\param[in] material the material for the new object's shape
	\param[in] density the density of the new object. Must be greater than zero.
	\param[in] shapeOffset an optional offset for the new shape, defaults to identity

	\return a new dynamic actor with the PxRigidBodyFlag, or NULL if it could 
	not be constructed

	@see PxRigidDynamic PxShapeFlag
*/

PxRigidDynamic*	PxCreateDynamic(PxPhysics& sdk,
								const PxTransform& transform,
								const PxGeometry& geometry,
								PxMaterial& material,
								PxReal density,
								const PxTransform& shapeOffset = PxTransform(PxIdentity));


/** \brief simple method to create a PxRigidDynamic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the transform of the new object
	\param[in] shape the shape of the new object
	\param[in] density the density of the new object. Must be greater than zero.

	\return a new dynamic actor with the PxRigidBodyFlag, or NULL if it could 
	not be constructed

	@see PxRigidDynamic PxShapeFlag
*/

PxRigidDynamic*	PxCreateDynamic(PxPhysics& sdk,
								const PxTransform& transform,
								PxShape& shape,
								PxReal density);


/** \brief simple method to create a kinematic PxRigidDynamic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the global pose of the new object
	\param[in] geometry the geometry of the new object's shape
	\param[in] material the material for the new object's shape
	\param[in] density the density of the new object. Must be greater than zero if the object is to participate in simulation.
	\param[in] shapeOffset an optional offset for the new shape, defaults to identity

	\note unlike PxCreateDynamic, the geometry is not restricted to box, capsule, sphere or convex. However, 
	kinematics of other geometry types may not participate in simulation collision and may be used only for
	triggers or scene queries of moving objects under animation control. In this case the density parameter
	will be ignored and the created shape will be set up as a scene query only shape (see #PxShapeFlag::eSCENE_QUERY_SHAPE)

	\return a new dynamic actor with the PxRigidBodyFlag::eKINEMATIC set, or NULL if it could 
	not be constructed

	@see PxRigidDynamic PxShapeFlag
*/

PxRigidDynamic*	PxCreateKinematic(PxPhysics& sdk,
								  const PxTransform& transform,
								  const PxGeometry& geometry,
								  PxMaterial& material,
								  PxReal density,
								  const PxTransform& shapeOffset = PxTransform(PxIdentity));


/** \brief simple method to create a kinematic PxRigidDynamic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the global pose of the new object
	\param[in] density the density of the new object. Must be greater than zero if the object is to participate in simulation.
	\param[in] shape the shape of the new object

	\note unlike PxCreateDynamic, the geometry is not restricted to box, capsule, sphere or convex. However, 
	kinematics of other geometry types may not participate in simulation collision and may be used only for
	triggers or scene queries of moving objects under animation control. In this case the density parameter
	will be ignored and the created shape will be set up as a scene query only shape (see #PxShapeFlag::eSCENE_QUERY_SHAPE)

	\return a new dynamic actor with the PxRigidBodyFlag::eKINEMATIC set, or NULL if it could 
	not be constructed

	@see PxRigidDynamic PxShapeFlag
*/

PxRigidDynamic*	PxCreateKinematic(PxPhysics& sdk,
								  const PxTransform& transform,
								  PxShape& shape,
								  PxReal density);


/** \brief simple method to create a PxRigidStatic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the global pose of the new object
	\param[in] geometry the geometry of the new object's shape
	\param[in] material the material for the new object's shape
	\param[in] shapeOffset an optional offset for the new shape, defaults to identity

	\return a new static actor, or NULL if it could not be constructed

	@see PxRigidStatic
*/

PxRigidStatic*	PxCreateStatic(PxPhysics& sdk,
							   const PxTransform& transform,
							   const PxGeometry& geometry,
							   PxMaterial& material,
							   const PxTransform& shapeOffset = PxTransform(PxIdentity));


/** \brief simple method to create a PxRigidStatic actor with a single PxShape. 

	\param[in] sdk the PxPhysics object
	\param[in] transform the global pose of the new object
	\param[in] shape the new object's shape

	\return a new static actor, or NULL if it could not be constructed

	@see PxRigidStatic
*/

PxRigidStatic*	PxCreateStatic(PxPhysics& sdk,
							   const PxTransform& transform,
							   PxShape& shape);


/**
\brief create a static body by copying attributes from another rigid actor

The function clones a PxRigidDynamic as a PxRigidStatic. A uniform scale is applied. The following properties are copied:
- shapes
- actor flags 
- owner client and client behavior bits

The following are not copied and retain their default values:
- name
- joints or observers
- aggregate or scene membership
- user data

\note Transforms are not copied with bit-exact accuracy.

\param[in] physicsSDK - the physics SDK used to allocate the rigid static
\param[in] actor the rigid actor from which to take the attributes.
\param[in] transform the transform of the new static.

\return the newly-created rigid static

*/

PxRigidStatic* PxCloneStatic(PxPhysics& physicsSDK, 
							 const PxTransform& transform,
							 const PxRigidActor& actor);


/**
\brief create a dynamic body by copying attributes from an existing body

The following properties are copied:
- shapes
- actor flags and rigidDynamic flags
- mass, moment of inertia, and center of mass frame
- linear and angular velocity
- linear and angular damping
- maximum angular velocity
- position and velocity solver iterations
- sleep threshold
- contact report threshold
- dominance group
- owner client and client behavior bits

The following are not copied and retain their default values:
- name
- joints or observers
- aggregate or scene membership
- sleep timer
- user data

\note Transforms are not copied with bit-exact accuracy.

\param[in] physicsSDK PxPhysics - the physics SDK used to allocate the rigid static
\param[in] body the rigid dynamic to clone.
\param[in] transform the transform of the new dynamic

\return the newly-created rigid static

*/

PxRigidDynamic*	PxCloneDynamic(PxPhysics& physicsSDK, 	 
							   const PxTransform& transform,
							   const PxRigidDynamic& body);


/** \brief create a plane actor. The plane equation is n.x + d = 0

	\param[in] sdk the PxPhysics object
	\param[in] plane a plane of the form n.x + d = 0
	\param[in] material the material for the new object's shape

	\return a new static actor, or NULL if it could not be constructed

	@see PxRigidStatic
*/

PxRigidStatic*	PxCreatePlane(PxPhysics& sdk,
							  const PxPlane& plane,
							  PxMaterial& material);


/**
\brief scale a rigid actor by a uniform scale

The geometry and relative positions of the actor are multiplied by the given scale value. If the actor is a rigid body or an
articulation link and the scaleMassProps value is true, the mass properties are scaled assuming the density is constant: the 
center of mass is linearly scaled, the mass is multiplied by the cube of the scale, and the inertia tensor by the fifth power of the scale. 

\param[in] actor a rigid actor
\param[in] scale the scale by which to multiply the actor
\param[in] scaleMassProps whether to scale the mass properties
*/

void PxScaleRigidActor(PxRigidActor& actor, PxReal scale, bool scaleMassProps = true);


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_EXTENSIONS_RIGIDBODY_H
#define PX_PHYSICS_EXTENSIONS_RIGIDBODY_H
/** \addtogroup extensions
  @{
*/

#include "PxPhysX.h"
#include "PxRigidBody.h"
#include "PxSceneQueryReport.h"
#include "PxFiltering.h"
#include "PxSceneQueryFiltering.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxBatchQuery;
class PxSweepCache;

/**
\brief utility functions for use with PxRigidBody and subclasses

@see PxRigidBody PxRigidDynamic PxArticulationLink
*/

class PxRigidBodyExt
{
public:
	/**
	\brief Computation of mass properties for a rigid body actor

	To simulate a dynamic rigid actor, the SDK needs a mass and an inertia tensor. 

	This method offers functionality to compute the necessary mass and inertia properties based on the shapes declared in
	the PxRigidBody descriptor and some additionally specified parameters. For each shape, the shape geometry, 
	the shape positioning within the actor and the specified shape density are used to compute the body's mass and 
	inertia properties.

	<ul>
	<li>Shapes without PxShapeFlag::eSIMULATION_SHAPE set are ignored. 
	<li>Shapes with plane, triangle mesh or heightfield geometry and PxShapeFlag::eSIMULATION_SHAPE set are not allowed for PxRigidBody collision</li>
	</ul>

	This method will set the mass, center of mass, and inertia tensor 

	if no collision shapes are found, the inertia tensor is set to (1,1,1) and the mass to 1

	if massLocalPose is non-NULL, the rigid body's center of mass parameter  will be set 
	to the user provided value (massLocalPose) and the inertia tensor will be resolved at that point.

	\note If all shapes of the actor have the same density then the overloaded method updateMassAndInertia() with a single density parameter can be used instead.

	\param[in,out] body The rigid body.
	\param[in] shapeDensities The per shape densities. There must be one entry for each shape which has the PxShapeFlag::eSIMULATION_SHAPE set. Other shapes are ignored. The density values must be greater than 0.
	\param[in] shapeDensityCount The number of provided density values.
	\param[in] massLocalPose The center of mass relative to the actor frame.  If set to null then (0,0,0) is assumed.
	\return Boolean. True on success else false.

	@see PxRigidBody::setMassLocalPose PxRigidBody::setMassSpaceInertia PxRigidBody::setMass
	*/
	static		bool			updateMassAndInertia(PxRigidBody& body, const PxReal* shapeDensities, PxU32 shapeDensityCount, const PxVec3* massLocalPose = NULL);


	/**
	\brief Computation of mass properties for a rigid body actor

	See previous method for details.

	\param[in,out] body The rigid body.
	\param[in] density The density of the body. Used to compute the mass of the body. The density must be greater than 0. 
	\param[in] massLocalPose The center of mass relative to the actor frame.  If set to null then (0,0,0) is assumed.
	\return Boolean. True on success else false.

	@see PxRigidBody::setMassLocalPose PxRigidBody::setMassSpaceInertia PxRigidBody::setMass
	*/
	static		bool			updateMassAndInertia(PxRigidBody& body, PxReal density, const PxVec3* massLocalPose = NULL);
	

	/**
	\brief Computation of mass properties for a rigid body actor

	This method sets the mass, inertia and center of mass of a rigid body. The mass is set to the sum of all user-supplied
	shape mass values, and the inertia and center of mass are computed according to the rigid body's shapes and the per shape mass input values.

	If no collision shapes are found, the inertia tensor is set to (1,1,1)

	\note If a single mass value should be used for the actor as a whole then the overloaded method setMassAndUpdateInertia() with a single mass parameter can be used instead.

	@see updateMassAndInertia for more details.

	\param[in,out] body The the rigid body for which to set the mass and centre of mass local pose properties.
	\param[in] shapeMasses The per shape mass values. There must be one entry for each shape which has the PxShapeFlag::eSIMULATION_SHAPE set. Other shapes are ignored. The mass values must be greater than 0.
	\param[in] shapeMassCount The number of provided mass values.
	\param[in] massLocalPose The center of mass relative to the actor frame. If set to null then (0,0,0) is assumed.
	\return Boolean. True on success else false.

	@see PxRigidBody::setCMassLocalPose PxRigidBody::setMassSpaceInertia PxRigidBody::setMass
	*/
	static		bool			setMassAndUpdateInertia(PxRigidBody& body, const PxReal* shapeMasses, PxU32 shapeMassCount, const PxVec3* massLocalPose = NULL);


	/**
	\brief Computation of mass properties for a rigid body actor

	This method sets the mass, inertia and center of mass of a rigid body. The mass is set to the user-supplied
	value, and the inertia and center of mass are computed according to the rigid body's shapes and the input mass.

	If no collision shapes are found, the inertia tensor is set to (1,1,1)

	@see updateMassAndInertia for more details.

	\param[in,out] body The the rigid body for which to set the mass and centre of mass local pose properties.
	\param[in] mass The mass of the body. Must be greater than 0.
	\param[in] massLocalPose The center of mass relative to the actor frame. If set to null then (0,0,0) is assumed.
	\return Boolean. True on success else false.

	@see PxRigidBody::setCMassLocalPose PxRigidBody::setMassSpaceInertia PxRigidBody::setMass
	*/
	static		bool			setMassAndUpdateInertia(PxRigidBody& body, PxReal mass, const PxVec3* massLocalPose = NULL);
	

	/**
	\brief Applies a force (or impulse) defined in the global coordinate frame, acting at a particular 
	point in global coordinates, to the actor. 

	Note that if the force does not act along the center of mass of the actor, this
	will also add the corresponding torque. Because forces are reset at the end of every timestep, 
	you can maintain a total external force on an object by calling this once every frame.

    ::PxForceMode determines if the force is to be conventional or impulsive.

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the wakeup parameter is true (default).

	\param[in] body The rigid body to apply the force to.
	\param[in] force Force/impulse to add, defined in the global frame. <b>Range:</b> force vector
	\param[in] pos Position in the global frame to add the force at. <b>Range:</b> position vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode). Only eFORCE and eIMPULSE are supported.
	\param[in] wakeup Specify if the call should wake up the actor.

	@see PxForceMode 
	@see addForceAtLocalPos() addLocalForceAtPos() addLocalForceAtLocalPos()
	*/
	static		void			addForceAtPos(PxRigidBody& body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE, bool wakeup = true);

	/**
	\brief Applies a force (or impulse) defined in the global coordinate frame, acting at a particular 
	point in local coordinates, to the actor. 

	Note that if the force does not act along the center of mass of the actor, this
	will also add the corresponding torque. Because forces are reset at the end of every timestep, you can maintain a
	total external force on an object by calling this once every frame.

	::PxForceMode determines if the force is to be conventional or impulsive.

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the wakeup parameter is true (default).

	\param[in] body The rigid body to apply the force to.
	\param[in] force Force/impulse to add, defined in the global frame. <b>Range:</b> force vector
	\param[in] pos Position in the local frame to add the force at. <b>Range:</b> position vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode). Only eFORCE and eIMPULSE are supported.
	\param[in] wakeup Specify if the call should wake up the actor.

	@see PxForceMode 
	@see addForceAtPos() addLocalForceAtPos() addLocalForceAtLocalPos()
	*/
	static		void			addForceAtLocalPos(PxRigidBody& body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE, bool wakeup = true);

	/**
	\brief Applies a force (or impulse) defined in the actor local coordinate frame, acting at a 
	particular point in global coordinates, to the actor. 

	Note that if the force does not act along the center of mass of the actor, this
	will also add the corresponding torque. Because forces are reset at the end of every timestep, you can maintain a
	total external force on an object by calling this once every frame.

	::PxForceMode determines if the force is to be conventional or impulsive.

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the wakeup parameter is true (default).

	\param[in] body The rigid body to apply the force to.
	\param[in] force Force/impulse to add, defined in the local frame. <b>Range:</b> force vector
	\param[in] pos Position in the global frame to add the force at. <b>Range:</b> position vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode). Only eFORCE and eIMPULSE are supported.
	\param[in] wakeup Specify if the call should wake up the actor.

	@see PxForceMode 
	@see addForceAtPos() addForceAtLocalPos() addLocalForceAtLocalPos()
	*/
	static		void			addLocalForceAtPos(PxRigidBody& body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE, bool wakeup = true);

	/**
	\brief Applies a force (or impulse) defined in the actor local coordinate frame, acting at a 
	particular point in local coordinates, to the actor. 

	Note that if the force does not act along the center of mass of the actor, this
	will also add the corresponding torque. Because forces are reset at the end of every timestep, you can maintain a
	total external force on an object by calling this once every frame.

	::PxForceMode determines if the force is to be conventional or impulsive.

	<b>Sleeping:</b> This call wakes the actor if it is sleeping and the wakeup parameter is true (default).

	\param[in] body The rigid body to apply the force to.
	\param[in] force Force/impulse to add, defined in the local frame. <b>Range:</b> force vector
	\param[in] pos Position in the local frame to add the force at. <b>Range:</b> position vector
	\param[in] mode The mode to use when applying the force/impulse(see #PxForceMode). Only eFORCE and eIMPULSE are supported.
	\param[in] wakeup Specify if the call should wake up the actor.

	@see PxForceMode 
	@see addForceAtPos() addForceAtLocalPos() addLocalForceAtPos()
	*/
	static		void			addLocalForceAtLocalPos(PxRigidBody& body, const PxVec3& force, const PxVec3& pos, PxForceMode::Enum mode = PxForceMode::eFORCE, bool wakeup = true);

	/**
	\brief Computes the velocity of a point given in world coordinates if it were attached to the 
	specified body and moving with it.

	\param[in] body The rigid body the point is attached to.
	\param[in] pos Position we wish to determine the velocity for, defined in the global frame. <b>Range:</b> position vector
	\return The velocity of point in the global frame.

	@see getLocalPointVelocity()
	*/
	static		PxVec3			getVelocityAtPos(const PxRigidBody& body, const PxVec3& pos);

	/**
	\brief Computes the velocity of a point given in local coordinates if it were attached to the 
	specified body and moving with it.

	\param[in] body The rigid body the point is attached to.
	\param[in] pos Position we wish to determine the velocity for, defined in the local frame. <b>Range:</b> position vector
	\return The velocity of point in the local frame.

	@see getLocalPointVelocity()
	*/
	static		PxVec3			getLocalVelocityAtLocalPos(const PxRigidBody& body, const PxVec3& pos);

	/**
	\brief Computes the velocity of a point (offset from the origin of the body) given in world coordinates if it were attached to the 
	specified body and moving with it.

	\param[in] body The rigid body the point is attached to.
	\param[in] pos Position (offset from the origin of the body) we wish to determine the velocity for, defined in the global frame. <b>Range:</b> position vector
	\return The velocity of point (offset from the origin of the body) in the global frame.

	@see getLocalPointVelocity()
	*/
	static		PxVec3			getVelocityAtOffset(const PxRigidBody& body, const PxVec3& pos);

	/**
	\brief Performs a linear sweep through space with the body's geometry objects.

	\note Supported geometries are: PxBoxGeometry, PxSphereGeometry, PxCapsuleGeometry. Other geometry types will be ignored.
	\note Internally this call is mapped to #PxBatchQuery::linearCompoundGeometrySweepSingle().

	The function sweeps all specified geometry objects through space and reports any objects in the scene
	which intersect. Apart from the number of objects intersected in this way, and the objects
	intersected, information on the closest intersection is put in an #PxSweepHit structure which 
	can be processed in the callback. See #PxSweepHit.

	\param[in] body The rigid body to sweep.
	\param[in] batchQuery The scene query object to process the query.
	\param[in] unitDir Normalized direction of the sweep.
	\param[in] distance Sweep distance. Needs to be larger than 0.
	\param[in] filterFlags Choose if to sweep against static, dynamic or both types of objects, or other filter logic. See #PxSceneQueryFilterFlags.
	\param[in] useShapeFilterData True if the filter data of the body shapes should be used for the query. False if no filtering is needed or separate filter data is provided.
	\param[in] filterDataList Custom filter data to use for each geometry object of the body. Only considered if useShapeFilterData is false.
	\param[in] filterDataCount Number of filter data entries
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query
	\param[in] sweepCache Sweep cache to use with the query

	\return returns the closest overlapping object.

	@see PxBatchQuery PxBatchQuery::linearCompoundGeometrySweepSingle PxSceneQueryFilterFlags PxFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSweepHit
	*/
	static		void			linearSweepSingle(PxRigidBody& body, PxBatchQuery& batchQuery, const PxVec3& unitDir, const PxReal distance, PxSceneQueryFilterFlags filterFlags, bool useShapeFilterData = true, PxFilterData* filterDataList=NULL, PxU32 filterDataCount=0, void* userData=NULL, const PxSweepCache* sweepCache=NULL);

	/**
	\brief Performs a linear sweep through space with the body's geometry objects, returning all overlaps.

	\note Supported geometries are: PxBoxGeometry, PxSphereGeometry, PxCapsuleGeometry. Other geometry types will be ignored.
	\note Internally this call is mapped to #PxBatchQuery::linearCompoundGeometrySweepMultiple().

	The function sweeps all geometry objects of the body through space and reports all objects in the scene
	which intersect. Apart from the number of objects intersected in this way, and the objects
	intersected, information on the closest intersection is put in an #PxSweepHit structure which 
	can be processed in the callback. See #PxSweepHit.

	\param[in] body The rigid body to sweep.
	\param[in] batchQuery The scene query object to process the query.
	\param[in] unitDir Normalized direction of the sweep.
	\param[in] distance Sweep distance. Needs to be larger than 0.
	\param[in] filterFlags Choose if to sweep against static, dynamic or both types of objects, or other filter logic. See #PxSceneQueryFilterFlags.
	\param[in] useShapeFilterData True if the filter data of the body shapes should be used for the query. False if no filtering is needed or separate filter data is provided.
	\param[in] filterDataList Custom filter data to use for each geometry object of the body. Only considered if useShapeFilterData is false.
	\param[in] filterDataCount Number of filter data entries
	\param[in] userData user can assign this to a value of his choice, usually to identify this particular query
	\param[in] sweepCache Sweep cache to use with the query

	\return returns the all overlapping objects.

	@see PxBatchQuery PxBatchQuery::linearCompoundGeometrySweepMultiple PxSceneQueryFilterFlags PxFilterData PxBatchQueryPreFilterShader PxBatchQueryPostFilterShader PxSweepHit
	*/
	static		void			linearSweepMultiple(PxRigidBody& body, PxBatchQuery& batchQuery, const PxVec3& unitDir, const PxReal distance,  PxSceneQueryFilterFlags filterFlags, bool useShapeFilterData = true, PxFilterData* filterDataList=NULL, PxU32 filterDataCount=0, void* userData=NULL, const PxSweepCache* sweepCache=NULL);

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

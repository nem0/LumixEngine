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


#ifndef PX_PHYSICS_NX_AGGREGATE
#define PX_PHYSICS_NX_AGGREGATE

/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "common/PxBase.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxActor;

/**
\brief Class to aggregate actors into a single broad phase entry.

A PxAggregate object is a collection of PxActors, which will exist as a single entry in the
broad-phase structures. This has 3 main benefits:

1) it reduces "broad phase pollution", where multiple objects of a single entity often overlap
   all the time (e.g. typically in a ragdoll).

2) it reduces broad-phase memory usage (which can be vital e.g. on SPU)

3) filtering can be optimized a lot if self-collisions within an aggregate are not needed. For
   example if you don't need collisions between ragdoll bones, it's faster to simply disable
   filtering once and for all, for the aggregate containing the ragdoll, rather than filtering
   out each bone-bone collision in the filter shader.

@see PxActor, PxPhysics.createAggregate
*/

class PxAggregate : public PxBase
{
public:

	/**
	\brief Deletes the aggregate object.

	Deleting the PxAggregate object does not delete the aggregated actors. If the PxAggregate object
	belongs to a scene, the aggregated actors are automatically re-inserted in that scene. If you intend
	to delete both the PxAggregate and its actors, it is best to release the actors first, then release
	the PxAggregate when it is empty.
	*/
	virtual	void		release()				= 0;

	/**
	\brief Adds an actor to the aggregate object.

	A warning is output if the total number of actors is reached, or if the incoming actor already belongs
	to an aggregate.

	If the aggregate belongs to a scene, adding an actor to the aggregate also adds the actor to that scene.

	If the actor already belongs to a scene, a warning is output and the call is ignored. You need to remove
	the actor from the scene first, before adding it to the aggregate.

	\param	[in] actor The actor that should be added to the aggregate
	return	true if success
	*/
	virtual	bool		addActor(PxActor& actor)		= 0;

	/**
	\brief Removes an actor from the aggregate object.

	A warning is output if the incoming actor does not belong to the aggregate. Otherwise the actor is
	removed from the aggregate. If the aggregate belongs to a scene, the actor is reinserted in that
	scene. If you intend to delete the actor, it is best to call #PxActor::release() directly. That way
	the actor will be automatically removed from its aggregate (if any) and not reinserted in a scene.

	\param	[in] actor The actor that should be removed from the aggregate
	return	true if success
	*/
	virtual	bool		removeActor(PxActor& actor)		= 0;

	/**
	\brief Adds an articulation to the aggregate object.

	A warning is output if the total number of actors is reached (every articulation link counts as an actor), 
	or if the incoming articulation already belongs	to an aggregate.

	If the aggregate belongs to a scene, adding an articulation to the aggregate also adds the articulation to that scene.

	If the articulation already belongs to a scene, a warning is output and the call is ignored. You need to remove
	the articulation from the scene first, before adding it to the aggregate.

	\param	[in] articulation The articulation that should be added to the aggregate
	return	true if success
	*/
	virtual	bool		addArticulation(PxArticulation& articulation) = 0;

	/**
	\brief Removes an articulation from the aggregate object.

	A warning is output if the incoming articulation does not belong to the aggregate. Otherwise the articulation is
	removed from the aggregate. If the aggregate belongs to a scene, the articulation is reinserted in that
	scene. If you intend to delete the articulation, it is best to call #PxArticulation::release() directly. That way
	the articulation will be automatically removed from its aggregate (if any) and not reinserted in a scene.

	\param	[in] articulation The articulation that should be removed from the aggregate
	return	true if success
	*/
	virtual	bool		removeArticulation(PxArticulation& articulation) = 0;

	/**
	\brief Returns the number of actors contained in the aggregate.

	You can use #getActors() to retrieve the actor pointers.

	\return Number of actors contained in the aggregate.

	@see PxActor getActors()
	*/
	virtual PxU32		getNbActors() const = 0;

	/**
	\brief Retrieves max amount of actors that can be contained in the aggregate.

	\return Max aggregate size. 

	@see PxPhysics::createAggregate()
	*/
	virtual	PxU32		getMaxNbActors() const = 0;

	/**
	\brief Retrieve all actors contained in the aggregate.

	You can retrieve the number of actor pointers by calling #getNbActors()

	\param[out] userBuffer The buffer to store the actor pointers.
	\param[in] bufferSize Size of provided user buffer.
	\param[in] startIndex Index of first actor pointer to be retrieved
	\return Number of actor pointers written to the buffer.

	@see PxShape getNbShapes()
	*/
	virtual PxU32		getActors(PxActor** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;

	/**
	\brief Retrieves the scene which this aggregate belongs to.

	\return Owner Scene. NULL if not part of a scene.

	@see PxScene
	*/
	virtual	PxScene*	getScene()	= 0;

	/**
	\brief Retrieves aggregate's self-collision flag.

	\return self-collision flag
	*/
	virtual	bool		getSelfCollision()	const	= 0;

	virtual	const char*	getConcreteTypeName() const	{ return "PxAggregate"; }

protected:
	PX_INLINE			PxAggregate(PxType concreteType, PxBaseFlags baseFlags) : PxBase(concreteType, baseFlags) {}
	PX_INLINE			PxAggregate(PxBaseFlags baseFlags) : PxBase(baseFlags) {}
	virtual				~PxAggregate() {}
	virtual	bool		isKindOf(const char* name) const { return !strcmp("PxAggregate", name) || PxBase::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

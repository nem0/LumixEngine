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


#ifndef PX_SIMULATION_EVENT_CALLBACK
#define PX_SIMULATION_EVENT_CALLBACK
/** \addtogroup physics
@{
*/

#include "foundation/PxVec3.h"
#include "PxPhysX.h"
#include "PxFiltering.h"
#include "PxContact.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxShape;
class PxActor;
class PxConstraint;

/**
\brief Collection of flags providing information on contact report pairs.

@see PxContactPairHeader
*/
struct PxContactPairHeaderFlag
{
	enum Enum
	{
		eDELETED_ACTOR_0				= (1<<0),	//!< The actor with index 0 has been deleted.
		eDELETED_ACTOR_1				= (1<<1),	//!< The actor with index 1 has been deleted.
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxContactPairHeaderFlag.

@see PxContactPairHeaderFlag
*/
typedef PxFlags<PxContactPairHeaderFlag::Enum, PxU16> PxContactPairHeaderFlags;
PX_FLAGS_OPERATORS(PxContactPairHeaderFlag::Enum, PxU16);


/**
\brief An Instance of this class is passed to PxSimulationEventCallback.onContact().

@see PxSimulationEventCallback.onContact()
*/
struct PxContactPairHeader
{
	public:
		PX_INLINE	PxContactPairHeader() {}

	/**
	\brief The two actors of the notification shape pairs.

	\note The actor pointers might reference deleted actors. This will be the case if PxPairFlag::eNOTIFY_TOUCH_LOST
		  or PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST events were requested for the pair and one of the involved actors 
		  gets deleted or removed from the scene. Check the #flags member to see whether that is the case.
		  Do not dereference a pointer to a deleted actor. The pointer to a deleted actor is only provided 
		  such that user data structures which might depend on the pointer value can be updated.

	@see PxActor
	*/
	PxActor*					actors[2];

	/**
	\brief Additional information on the contact report pair.

	@see PxContactPairHeaderFlag
	*/
	PxContactPairHeaderFlags	flags;
};


/**
\brief Collection of flags providing information on contact report pairs.

@see PxContactPair
*/
struct PxContactPairFlag
{
	enum Enum
	{
		/**
		\brief The shape with index 0 has been deleted.
		*/
		eDELETED_SHAPE_0				= (1<<0),

		/**
		\brief The shape with index 1 has been deleted.
		*/
		eDELETED_SHAPE_1				= (1<<1),
		
		/**
		\brief First actor pair contact.

		The provided shape pair marks the first contact between the two actors, no other shape pair has been touching prior to the current simulation frame.

		\note: This info is only available if #PxPairFlag::eNOTIFY_TOUCH_FOUND has been declared for the pair.
		*/
		eACTOR_PAIR_HAS_FIRST_TOUCH		= (1<<2),

		/**
		\brief All contact between the actor pair was lost.

		All contact between the two actors has been lost, no shape pairs remain touching after the current simulation frame.
		*/
		eACTOR_PAIR_LOST_TOUCH			= (1<<3),

		/**
		\brief Internal flag, used by #PxContactPair.extractContacts()

		For meshes/heightfields the flag indicates that the contact points provide internal triangle index information.
		*/
		eINTERNAL_HAS_FACE_INDICES		= (1<<4),

		/**
		\brief Internal flag, used by #PxContactPair.extractContacts()

		The applied contact impulses are provided for every contact point. 
		This is the case if #PxPairFlag::eRESOLVE_CONTACTS has been set for the pair.
		*/
		eINTERNAL_HAS_IMPULSES			= (1<<5),

		/**
		\brief Internal flag, used by #PxContactPair.extractContacts()

		The provided contact point information is flipped with regards to the shapes of the contact pair. This mainly concerns the order of the internal triangle indices.
		*/
		eINTERNAL_CONTACTS_ARE_FLIPPED	= (1<<6)
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxContactPairFlag.

@see PxContactPairFlag
*/
typedef PxFlags<PxContactPairFlag::Enum, PxU16> PxContactPairFlags;
PX_FLAGS_OPERATORS(PxContactPairFlag::Enum, PxU16);


/**
\brief A contact point as used by contact notification
*/
struct PxContactPairPoint
{
	/**
	\brief The position of the contact point between the shapes, in world space. 
	*/
	PxVec3	position;

	/**
	\brief The separation of the shapes at the contact point.  A negative separation denotes a penetration.
	*/
	PxReal	separation;

	/**
	\brief The normal of the contacting surfaces at the contact point.  
	*/
	PxVec3	normal;

	/**
	\brief The surface index of shape 0 at the contact point.  This is used to identify the surface material.
	*/
	PxU32   internalFaceIndex0;

	/**
	\brief The impulse applied at the contact point, in world space. Divide by the simulation time step to get a force value.
	*/
	PxVec3	impulse;

	/**
	\brief The surface index of shape 1 at the contact point.  This is used to identify the surface material.
	*/
	PxU32   internalFaceIndex1;
};


/**
\brief Contact report pair information.

Instances of this class are passed to PxSimulationEventCallback.onContact(). If contact reports have been requested for a pair of shapes (see #PxPairFlag),
then the corresponding contact information will be provided through this structure.

@see PxSimulationEventCallback.onContact()
*/
struct PxContactPair
{
	public:
		PX_INLINE	PxContactPair() {}

	/**
	\brief The two shapes that make up the pair.

	\note The shape pointers might reference deleted shapes. This will be the case if #PxPairFlag::eNOTIFY_TOUCH_LOST
		  or #PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST events were requested for the pair and one of the involved shapes 
		  gets deleted. Check the #flags member to see whether that is the case. Do not dereference a pointer to a 
		  deleted shape. The pointer to a deleted shape is only provided such that user data structures which might 
		  depend on the pointer value can be updated.

	@see PxShape
	*/
	PxShape*				shapes[2];

	/**
	\brief Contact stream containing contact point data

	This pointer is only valid if contact point information has been requested for the contact report pair (see #PxPairFlag::eNOTIFY_CONTACT_POINTS).
	Use #extractContacts() as a reference for the data layout of the stream.
	*/
	const PxU8*				contactStream;

	/**
	\brief Size of the contact stream [bytes]
	*/
	PxU32					requiredBufferSize;

	/**
	\brief Number of contact points stored in the contact stream
	*/
	PxU16					contactCount;

	/**
	\brief Additional information on the contact report pair.

	@see PxContactPairFlag
	*/
	PxContactPairFlags		flags;

	/**
	\brief Flags raised due to the contact.

	The events field is a combination of:

	<ul>
	<li>PxPairFlag::eNOTIFY_TOUCH_FOUND,</li>
	<li>PxPairFlag::eNOTIFY_TOUCH_PERSISTS,</li>
	<li>PxPairFlag::eNOTIFY_TOUCH_LOST,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST</li>
	</ul>

	See the documentation of #PxPairFlag for an explanation of each.

	@see PxPairFlag
	*/
	PxPairFlags				events;

	PxU32					internalData[2];	// For internal use only

	/**
	\brief Extracts the contact points from the stream and stores them in a convenient format.
	
	\param[in] userBuffer Array of PxContactPairPoint structures to extract the contact points to. The number of contacts for a pair is defined by #contactCount
	\param[in] bufferSize Number of PxContactPairPoint structures the provided buffer can store.
	\return Number of contact points written to the buffer.

	@see PxContactPairPoint
	*/
	PX_INLINE PxU32          extractContacts(PxContactPairPoint* userBuffer, PxU32 bufferSize) const;
};


PX_INLINE PxU32 PxContactPair::extractContacts(PxContactPairPoint* userBuffer, PxU32 bufferSize) const
{
	const PxU8* stream = contactStream;

	const PxContactPoint* contacts = reinterpret_cast<const PxContactPoint*>(stream);
	stream += contactCount * sizeof(PxContactPoint);

	const PxReal* impulses = reinterpret_cast<const PxReal*>(stream);

	PxU32 nbContacts = PxMin((PxU32)contactCount, bufferSize);

	PxU32 flippedContacts = (flags & PxContactPairFlag::eINTERNAL_CONTACTS_ARE_FLIPPED);
	PxU32 hasImpulses = (flags & PxContactPairFlag::eINTERNAL_HAS_IMPULSES);

	for(PxU32 i=0; i < nbContacts; i++)
	{
		const PxContactPoint& cp = contacts[i];
		PxContactPairPoint& dst = userBuffer[i];
		dst.position = cp.point;
		dst.separation = cp.separation;
		dst.normal = cp.normal;
		if (!flippedContacts)
		{
			dst.internalFaceIndex0 = cp.internalFaceIndex0;
			dst.internalFaceIndex1 = cp.internalFaceIndex1;
		}
		else
		{
			dst.internalFaceIndex0 = cp.internalFaceIndex1;
			dst.internalFaceIndex1 = cp.internalFaceIndex0;
		}

		if (hasImpulses)
		{
			PxReal impulse = impulses[i];
			dst.impulse = dst.normal * impulse;
		}
		else
			dst.impulse = PxVec3(0.0f);
	}

	return nbContacts;
}


/**
\brief Collection of flags providing information on trigger report pairs.

@see PxTriggerPair
*/
struct PxTriggerPairFlag
{
	enum Enum
	{
		eDELETED_SHAPE_TRIGGER			= (1<<0),	//!< The trigger shape has been deleted.
		eDELETED_SHAPE_OTHER			= (1<<1),	//!< The shape causing the trigger event has been deleted.
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxTriggerPairFlag.

@see PxTriggerPairFlag
*/
typedef PxFlags<PxTriggerPairFlag::Enum, PxU8> PxTriggerPairFlags;
PX_FLAGS_OPERATORS(PxTriggerPairFlag::Enum, PxU8);


/**
\brief Descriptor for a trigger pair.

An array of these structs gets passed to the PxSimulationEventCallback::onTrigger() report.

\note The shape pointers might reference deleted shapes. This will be the case if #PxPairFlag::eNOTIFY_TOUCH_LOST
      events were requested for the pair and one of the involved shapes gets deleted. Check the #flags member to see
	  whether that is the case. Do not dereference a pointer to a deleted shape. The pointer to a deleted shape is 
	  only provided such that user data structures which might depend on the pointer value can be updated.

@see PxSimulationEventCallback.onTrigger()
*/
struct PxTriggerPair
{
	PX_INLINE PxTriggerPair() {}

	PxShape*				triggerShape;	//!< The shape that has been marked as a trigger.
	PxShape*				otherShape;		//!< The shape causing the trigger event.
	PxPairFlag::Enum		status;			//!< Type of trigger event (eNOTIFY_TOUCH_FOUND, eNOTIFY_TOUCH_PERSISTS or eNOTIFY_TOUCH_LOST). eNOTIFY_TOUCH_PERSISTS is deprecated and will be removed in the next release.
	PxTriggerPairFlags		flags;			//!< Additional information on the pair
};


/**
\brief Descriptor for a broken constraint.

An array of these structs gets passed to the PxSimulationEventCallback::onConstraintBreak() report.

@see PxConstraint PxSimulationEventCallback.onConstraintBreak()
*/
struct PxConstraintInfo
{
	PX_INLINE PxConstraintInfo() {}
	PX_INLINE PxConstraintInfo(PxConstraint* c, void* extRef, PxU32 t) : constraint(c), externalReference(extRef), type(t) {}

	PxConstraint*	constraint;				//!< The broken constraint.
	void*			externalReference;		//!< The external object which owns the constraint (see #PxConstraintConnector::getExternalReference())
	PxU32			type;					//!< Unique type ID of the external object. Allows to cast the provided external reference to the appropriate type
};


/**
 \brief An interface class that the user can implement in order to receive simulation events.

  \note SDK state should not be modified from within the callbacks. In particular objects should not
  be created or destroyed. If state modification is needed then the changes should be stored to a buffer
  and performed after the simulation step.

  <b>Threading:</b> It is not necessary to make this class thread safe as it will only be called in the context of the
  user thread.

 @see PxScene.setSimulationEventCallback() PxScene.getSimulationEventCallback()
*/
class PxSimulationEventCallback
	{
	public:
	/**
	\brief This is called when a breakable constraint breaks.
	
	\note The user should not release the constraint shader inside this call!

	\param[in] constraints - The constraints which have been broken.
	\param[in] count       - The number of constraints

	@see PxConstraint PxConstraintDesc.linearBreakForce PxConstraintDesc.angularBreakForce
	*/
	virtual void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) = 0;

	/**
	\brief This is called during PxScene::fetchResults with the actors which have just been woken up.

	\note Only supported by rigid bodies yet.

	\param[in] actors - The actors which just woke up.
	\param[in] count  - The number of actors

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback
	*/
	virtual void onWake(PxActor** actors, PxU32 count) = 0;

	/**
	\brief This is called during PxScene::fetchResults with the actors which have just been put to sleep.

	\note Only supported by rigid bodies yet.

	\param[in] actors - The actors which have just been put to sleep.
	\param[in] count  - The number of actors

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback
	*/
	virtual void onSleep(PxActor** actors, PxU32 count) = 0;

	/**
	\brief The user needs to implement this interface class in order to be notified when
	certain contact events occur.

	The method will be called for a pair of actors if one of the colliding shape pairs requested contact notification.
	You request which events are reported using the filter shader/callback mechanism (see #PxSimulationFilterShader,
	#PxSimulationFilterCallback, #PxPairFlag).
	
	Do not keep references to the passed objects, as they will be 
	invalid after this function returns.

	\param[in] pairHeader Information on the two actors whose shapes triggered a contact report.
	\param[in] pairs The contact pairs of two actors for which contact reports have been requested. See #PxContactPair.
	\param[in] nbPairs The number of provided contact pairs.

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback PxContactPair PxPairFlag PxSimulationFilterShader PxSimulationFilterCallback
	*/
	virtual void onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) = 0;

	/*
	\brief This is called during PxScene::fetchResults with the current trigger pair events.

	Shapes which have been marked as triggers using PxShapeFlag::eTRIGGER_SHAPE will send events
	according to the pair flag specification in the filter shader (see #PxPairFlag, #PxSimulationFilterShader).

	\param[in] pairs - The trigger pairs which caused events.
	\param[in] count - The number of trigger pairs.

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback PxPairFlag PxSimulationFilterShader PxShapeFlag PxShape.setFlag()
	*/
	virtual void onTrigger(PxTriggerPair* pairs, PxU32 count) = 0;

	virtual ~PxSimulationEventCallback() {}
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

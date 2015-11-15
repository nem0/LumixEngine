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


#ifndef PX_SIMULATION_EVENT_CALLBACK
#define PX_SIMULATION_EVENT_CALLBACK
/** \addtogroup physics
@{
*/

#include "foundation/PxVec3.h"
#include "foundation/PxTransform.h"
#include "foundation/PxMemory.h"
#include "PxPhysXConfig.h"
#include "PxFiltering.h"
#include "PxContact.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxShape;
class PxActor;
class PxRigidActor;
class PxConstraint;


/**
\brief Extra data item types for contact pairs.

@see PxContactPairExtraDataItem.type
*/
struct PxContactPairExtraDataType
{
	enum Enum
	{
		ePRE_SOLVER_VELOCITY,	//!< see #PxContactPairVelocity
		ePOST_SOLVER_VELOCITY,	//!< see #PxContactPairVelocity
		eCONTACT_EVENT_POSE,	//!< see #PxContactPairPose
		eCONTACT_PAIR_INDEX  	//!< see #PxContactPairIndex
	};
};


/**
\brief Base class for items in the extra data stream of contact pairs

@see PxContactPairHeader.extraDataStream
*/
struct PxContactPairExtraDataItem
{
public:
	PX_FORCE_INLINE PxContactPairExtraDataItem() {}

	/**
	\brief The type of the extra data stream item
	*/
	PxU8 type;
};


/**
\brief Velocities of the contact pair rigid bodies

This struct is shared by multiple types of extra data items. The #type field allows to distinguish between them:
\li PxContactPairExtraDataType::ePRE_SOLVER_VELOCITY: see #PxPairFlag::ePRE_SOLVER_VELOCITY
\li PxContactPairExtraDataType::ePOST_SOLVER_VELOCITY: see #PxPairFlag::ePOST_SOLVER_VELOCITY

\note For static rigid bodies, the velocities will be set to zero.

@see PxContactPairHeader.extraDataStream
*/
struct PxContactPairVelocity : public PxContactPairExtraDataItem
{
public:
	PX_FORCE_INLINE PxContactPairVelocity() {}

	/**
	\brief The linear velocity of the rigid bodies
	*/
	PxVec3 linearVelocity[2];
	
	/**
	\brief The angular velocity of the rigid bodies
	*/
	PxVec3 angularVelocity[2];
};


/**
\brief World space actor poses of the contact pair rigid bodies

@see PxContactPairHeader.extraDataStream PxPairFlag::eCONTACT_EVENT_POSE
*/
struct PxContactPairPose : public PxContactPairExtraDataItem
{
public:
	PX_FORCE_INLINE PxContactPairPose() {}

	/**
	\brief The world space pose of the rigid bodies
	*/
	PxTransform globalPose[2];
};


/**
\brief Marker for the beginning of a new item set in the extra data stream.

If CCD with multiple passes is enabled, then a fast moving object might bounce on and off the same
object multiple times. Also, different shapes of the same actor might gain and lose contact with an other
object over multiple passes. This marker allows to seperate the extra data items for each collision case, as well as
distinguish the shape pair reports of different CCD passes.

Example:
Let us assume that an actor a0 with shapes s0_0 and s0_1 hits another actor a1 with shape s1.
First s0_0 will hit s1, then a0 will slightly rotate and s0_1 will hit s1 while s0_0 will lose contact with s1.
Furthermore, let us say that contact event pose information is requested as extra data.
The extra data stream will look like this:

PxContactPairIndexA | PxContactPairPoseA | PxContactPairIndexB | PxContactPairPoseB

The corresponding array of PxContactPair events (see #PxSimulationEventCallback.onContact()) will look like this:

PxContactPair(touch_found: s0_0, s1) | PxContactPair(touch_lost: s0_0, s1) | PxContactPair(touch_found: s0_1, s1)
 
The #index of PxContactPairIndexA will point to the first entry in the PxContactPair array, for PxContactPairIndexB,
#index will point to the third entry.

@see PxContactPairHeader.extraDataStream
*/
struct PxContactPairIndex : public PxContactPairExtraDataItem
{
public:
	PX_FORCE_INLINE PxContactPairIndex() {}

	/**
	\brief The next item set in the extra data stream refers to the contact pairs starting at #index in the reported PxContactPair array.
	*/
	PxU16 index;
};


/**
\brief A class to iterate over a contact pair extra data stream.

@see PxContactPairHeader.extraDataStream
*/
struct PxContactPairExtraDataIterator
{
	/**
	\brief Constructor
	\param[in] stream Pointer to the start of the stream.
	\param[in] size Size of the stream in bytes.
	*/
	PX_FORCE_INLINE PxContactPairExtraDataIterator(const PxU8* stream, PxU32 size) 
		: currPtr(stream), endPtr(stream + size), contactPairIndex(0)
	{
		clearDataPtrs();
	}

	/**
	\brief Advances the iterator to next set of extra data items.
	
	The contact pair extra data stream contains sets of items as requested by the corresponding #PxPairFlag flags
	#PxPairFlag::ePRE_SOLVER_VELOCITY, #PxPairFlag::ePOST_SOLVER_VELOCITY, #PxPairFlag::eCONTACT_EVENT_POSE. A set can contain one
	item of each plus the PxContactPairIndex item. This method parses the stream and points the iterator
	member variables to the corresponding items of the current set, if they are available. If CCD is not enabled,
	you should only get one set of items. If CCD with multiple passes is enabled, you might get more than one item
	set.

	\note Even though contact pair extra data is requested per shape pair, you will not get an item set per shape pair
	but one per actor pair. If, for example, an actor has two shapes and both collide with another actor, then
	there will only be one item set (since it applies to both shape pairs).
	
	\return True if there was another set of extra data items in the stream, else false.
	
	@see PxContactPairVelocity PxContactPairPose PxContactPairIndex
	*/
	PX_INLINE bool nextItemSet()
	{
		clearDataPtrs();
		
		bool foundEntry = false;
		bool endOfItemSet = false;
		while ((currPtr < endPtr) && (!endOfItemSet))
		{
			const PxContactPairExtraDataItem* edItem = reinterpret_cast<const PxContactPairExtraDataItem*>(currPtr);
			PxU8 type = edItem->type;

			switch(type)
			{
				case PxContactPairExtraDataType::ePRE_SOLVER_VELOCITY:
				{
					PX_ASSERT(!preSolverVelocity);
					preSolverVelocity = static_cast<const PxContactPairVelocity*>(edItem);
					currPtr += sizeof(PxContactPairVelocity);
					foundEntry = true;
				}
				break;
				
				case PxContactPairExtraDataType::ePOST_SOLVER_VELOCITY:
				{
					postSolverVelocity = static_cast<const PxContactPairVelocity*>(edItem);
					currPtr += sizeof(PxContactPairVelocity);
					foundEntry = true;
				}
				break;
				
				case PxContactPairExtraDataType::eCONTACT_EVENT_POSE:
				{
					eventPose = static_cast<const PxContactPairPose*>(edItem);
					currPtr += sizeof(PxContactPairPose);
					foundEntry = true;
				}
				break;
				
				case PxContactPairExtraDataType::eCONTACT_PAIR_INDEX:
				{
					if (!foundEntry)
					{
						contactPairIndex = static_cast<const PxContactPairIndex*>(edItem)->index;
						currPtr += sizeof(PxContactPairIndex);
						foundEntry = true;
					}
					else
						endOfItemSet = true;
				}
				break;
				
				default:
					return foundEntry;
			}
		}
		
		return foundEntry;
	}

private:
	/**
	\brief Internal helper
	*/
	PX_FORCE_INLINE void clearDataPtrs()
	{
		preSolverVelocity = NULL;
		postSolverVelocity = NULL;
		eventPose = NULL;
	}
	
public:
	/**
	\brief Current pointer in the stream.
	*/
	const PxU8* currPtr;
	
	/**
	\brief Pointer to the end of the stream.
	*/
	const PxU8* endPtr;
	
	/**
	\brief Pointer to the current pre solver velocity item in the stream. NULL if there is none.
	
	@see PxContactPairVelocity
	*/
	const PxContactPairVelocity* preSolverVelocity;
	
	/**
	\brief Pointer to the current post solver velocity item in the stream. NULL if there is none.
	
	@see PxContactPairVelocity
	*/
	const PxContactPairVelocity* postSolverVelocity;
	
	/**
	\brief Pointer to the current contact event pose item in the stream. NULL if there is none.
	
	@see PxContactPairPose
	*/
	const PxContactPairPose* eventPose;
	
	/**
	\brief The contact pair index of the current item set in the stream.
	
	@see PxContactPairIndex
	*/
	PxU32 contactPairIndex;
};


/**
\brief Collection of flags providing information on contact report pairs.

@see PxContactPairHeader
*/
struct PxContactPairHeaderFlag
{
	enum Enum
	{
		eREMOVED_ACTOR_0				= (1<<0),			//!< The actor with index 0 has been removed from the scene.
		eREMOVED_ACTOR_1				= (1<<1),			//!< The actor with index 1 has been removed from the scene.
		PX_DEPRECATED eDELETED_ACTOR_0	= eREMOVED_ACTOR_0,	//!< \deprecated use eREMOVED_ACTOR_0 instead
		PX_DEPRECATED eDELETED_ACTOR_1	= eREMOVED_ACTOR_1	//!< \deprecated use eREMOVED_ACTOR_1 instead
		
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxContactPairHeaderFlag.

@see PxContactPairHeaderFlag
*/
typedef PxFlags<PxContactPairHeaderFlag::Enum, PxU16> PxContactPairHeaderFlags;
PX_FLAGS_OPERATORS(PxContactPairHeaderFlag::Enum, PxU16)


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
	PxRigidActor*				actors[2];

	/**
	\brief Stream containing extra data as requested in the PxPairFlag flags of the simulation filter.

	This pointer is only valid if any kind of extra data information has been requested for the contact report pair (see #PxPairFlag::ePOST_SOLVER_VELOCITY etc.),
	else it will be NULL.
	
	@see PxPairFlag
	*/
	const PxU8*					extraDataStream;
	
	/**
	\brief Size of the extra data stream [bytes] 
	*/
	PxU16						extraDataStreamSize;

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
		\brief The shape with index 0 has been removed from the actor/scene.
		*/
		eREMOVED_SHAPE_0				= (1<<0),

		/**
		\brief The shape with index 1 has been removed from the actor/scene.
		*/
		eREMOVED_SHAPE_1				= (1<<1),

		/**
		\deprecated use eREMOVED_SHAPE_0 instead
		*/
		PX_DEPRECATED eDELETED_SHAPE_0	= eREMOVED_SHAPE_0,

		/**
		\deprecated use eREMOVED_SHAPE_1 instead
		*/
		PX_DEPRECATED eDELETED_SHAPE_1	= eREMOVED_SHAPE_1,
		
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
		This is the case if #PxPairFlag::eSOLVE_CONTACT has been set for the pair.
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
PX_FLAGS_OPERATORS(PxContactPairFlag::Enum, PxU16)


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
	\brief The normal of the contacting surfaces at the contact point. The normal direction points from the second shape to the first shape.
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
	\brief Size of the contact stream [bytes] including force buffer
	*/
	PxU32					requiredBufferSize;

	/**
	\brief Number of contact points stored in the contact stream
	*/
	PxU16					contactCount;

	/**
	\brief Size of the contact stream [bytes] not including force buffer
	*/

	PxU16					contactStreamSize;

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
	<li>PxPairFlag::eNOTIFY_TOUCH_CCD,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_FOUND,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_PERSISTS,</li>
	<li>PxPairFlag::eNOTIFY_THRESHOLD_FORCE_LOST</li>
	</ul>

	See the documentation of #PxPairFlag for an explanation of each.

	\note eNOTIFY_TOUCH_CCD can get raised even if the pair did not request this event. However, in such a case it will only get
	raised in combination with one of the other flags to point out that the other event occured during a CCD pass.

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
	PX_INLINE PxU32			extractContacts(PxContactPairPoint* userBuffer, PxU32 bufferSize) const;

	/**
	\brief Helper method to clone the contact pair and copy the contact data stream into a user buffer.
	
	The contact data stream is only accessible during the contact report callback. This helper function provides copy functionality
	to buffer the contact stream information such that it can get accessed at a later stage.

	\param[in] newPair The contact pair info will get copied to this instance. The contact data stream pointer of the copy will be redirected to the provided user buffer. Use NULL to skip the contact pair copy operation.
	\param[in] bufferMemory Memory block to store the contact data stream to. At most #requiredBufferSize bytes will get written to the buffer.
	*/
	PX_INLINE void			bufferContacts(PxContactPair* newPair, PxU8* bufferMemory) const;
};


PX_INLINE PxU32 PxContactPair::extractContacts(PxContactPairPoint* userBuffer, PxU32 bufferSize) const
{
	const PxU8* stream = contactStream;

	PxU32 nbContacts = 0;

	if(contactCount && bufferSize)
	{
		PxContactStreamIterator iter((PxU8*)stream, contactStreamSize);

		stream += ((contactStreamSize + 15) & ~15);

		const PxReal* impulses = reinterpret_cast<const PxReal*>(stream);

		PxU32 flippedContacts = (flags & PxContactPairFlag::eINTERNAL_CONTACTS_ARE_FLIPPED);
		PxU32 hasImpulses = (flags & PxContactPairFlag::eINTERNAL_HAS_IMPULSES);


		while(iter.hasNextPatch())
		{
			iter.nextPatch();
			while(iter.hasNextContact())
			{
				iter.nextContact();
				PxContactPairPoint& dst = userBuffer[nbContacts];
				dst.position = iter.getContactPoint();
				dst.separation = iter.getSeparation();
				dst.normal = iter.getContactNormal();
				if (!flippedContacts)
				{
					dst.internalFaceIndex0 = iter.getFaceIndex0();
					dst.internalFaceIndex1 = iter.getFaceIndex1();
				}
				else
				{
					dst.internalFaceIndex0 = iter.getFaceIndex1();
					dst.internalFaceIndex1 = iter.getFaceIndex0();
				}

				if (hasImpulses)
				{
					PxReal impulse = impulses[nbContacts];
					dst.impulse = dst.normal * impulse;
				}
				else
					dst.impulse = PxVec3(0.0f);
				++nbContacts;
				if(nbContacts == bufferSize)
					return nbContacts;
			}
		}
	}

	return nbContacts;
}


PX_INLINE void PxContactPair::bufferContacts(PxContactPair* newPair, PxU8* bufferMemory) const
{
	if (newPair)
	{
		*newPair = *this;
		newPair->contactStream = bufferMemory;
	}

	if (contactStream)
		PxMemCopy(bufferMemory, contactStream, requiredBufferSize);
}


/**
\brief Collection of flags providing information on trigger report pairs.

@see PxTriggerPair
*/
struct PxTriggerPairFlag
{
	enum Enum
	{
		eREMOVED_SHAPE_TRIGGER					= (1<<0),					//!< The trigger shape has been removed from the actor/scene.
		eREMOVED_SHAPE_OTHER					= (1<<1),					//!< The shape causing the trigger event has been removed from the actor/scene.
		PX_DEPRECATED eDELETED_SHAPE_TRIGGER	= eREMOVED_SHAPE_TRIGGER,	//!< \deprecated use eREMOVED_SHAPE_TRIGGER instead
		PX_DEPRECATED eDELETED_SHAPE_OTHER		= eREMOVED_SHAPE_OTHER,		//!< \deprecated use eREMOVED_SHAPE_OTHER instead
		eNEXT_FREE								= (1<<2)					//!< For internal use only.
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxTriggerPairFlag.

@see PxTriggerPairFlag
*/
typedef PxFlags<PxTriggerPairFlag::Enum, PxU8> PxTriggerPairFlags;
PX_FLAGS_OPERATORS(PxTriggerPairFlag::Enum, PxU8)


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
	PxRigidActor*			triggerActor;	//!< The actor to which triggerShape is attached
	PxShape*				otherShape;		//!< The shape causing the trigger event. If collision between trigger shapes is enabled, then this member might point to a trigger shape as well.
	PxRigidActor*			otherActor;		//!< The actor to which otherShape is attached
	PxPairFlag::Enum		status;			//!< Type of trigger event (eNOTIFY_TOUCH_FOUND or eNOTIFY_TOUCH_LOST). eNOTIFY_TOUCH_PERSISTS events are not supported.
	PxTriggerPairFlags		flags;			//!< Additional information on the pair (see #PxTriggerPairFlag)
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

	\note No event will get reported if the constraint breaks but gets deleted while the time step is still being simulated.

	\param[in] constraints - The constraints which have been broken.
	\param[in] count       - The number of constraints

	@see PxConstraint PxConstraintDesc.linearBreakForce PxConstraintDesc.angularBreakForce
	*/
	virtual void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) = 0;

	/**
	\brief This is called during PxScene::fetchResults with the actors which have just been woken up.

	\note Only supported by rigid bodies yet.
	\note Only called on actors for which the PxActorFlag eSEND_SLEEP_NOTIFIES has been set.
	\note Only the latest sleep state transition happening between fetchResults() of the previous frame and fetchResults() of the current frame
	will get reported. For example, let us assume actor A is awake, then A->putToSleep() gets called, then later A->wakeUp() gets called.
	At the next simulate/fetchResults() step only an onWake() event will get triggered because that was the last transition.
	\note If an actor gets newly added to a scene with properties such that it is awake and the sleep state does not get changed by 
	the user or simulation, then an onWake() event will get sent at the next simulate/fetchResults() step.

	\param[in] actors - The actors which just woke up.
	\param[in] count  - The number of actors

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback PxActorFlag PxActor.setActorFlag()
	*/
	virtual void onWake(PxActor** actors, PxU32 count) = 0;

	/**
	\brief This is called during PxScene::fetchResults with the actors which have just been put to sleep.

	\note Only supported by rigid bodies yet.
	\note Only called on actors for which the PxActorFlag eSEND_SLEEP_NOTIFIES has been set.
	\note Only the latest sleep state transition happening between fetchResults() of the previous frame and fetchResults() of the current frame
	will get reported. For example, let us assume actor A is asleep, then A->wakeUp() gets called, then later A->putToSleep() gets called.
	At the next simulate/fetchResults() step only an onSleep() event will get triggered because that was the last transition (assuming the simulation
	does not wake the actor up).
	\note If an actor gets newly added to a scene with properties such that it is asleep and the sleep state does not get changed by 
	the user or simulation, then an onSleep() event will get sent at the next simulate/fetchResults() step.

	\param[in] actors - The actors which have just been put to sleep.
	\param[in] count  - The number of actors

	@see PxScene.setSimulationEventCallback() PxSceneDesc.simulationEventCallback PxActorFlag PxActor.setActorFlag()
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

	/**
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

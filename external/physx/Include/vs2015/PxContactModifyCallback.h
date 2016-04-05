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


#ifndef PX_CONTACT_MODIFY_CALLBACK
#define PX_CONTACT_MODIFY_CALLBACK
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "PxShape.h"
#include "PxContact.h"
#include "foundation/PxTransform.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxShape;

/**
\brief An array of contact points, as passed to contact modification.

The word 'set' in the name does not imply that duplicates are filtered in any 
way.  This initial set of contacts does potentially get reduced to a smaller 
set before being passed to the solver.

You can use the accessors to read and write contact properties.  The number of 
contacts is immutable, other than being able to disable contacts using ignore().

@see PxContactModifyCallback, PxModifiableContact
*/
class PxContactSet
{
public:
	/**
	\brief Get the position of a specific contact point in the set.

	The contact points could be on the surface of either shape and there are currently no guarantees provided upon which shape the points lie.

	@see PxModifiableContact.point
	*/
	PX_FORCE_INLINE		const PxVec3& getPoint(PxU32 i) const			{  return mContacts[i].contact;		}

	/**
	\brief Alter the position of a specific contact point in the set.

	@see PxModifiableContact.point
	*/
	PX_FORCE_INLINE		void setPoint(PxU32 i, const PxVec3& p)			
	{
		mContacts[i].contact = p; 
	}

	/**
	\brief Get the contact normal of a specific contact point in the set.

	The contact normal points from the second shape to the first shape.

	@see PxModifiableContact.normal
	*/
	PX_FORCE_INLINE		const PxVec3& getNormal(PxU32 i) const			{ return mContacts[i].normal;	}

	/**
	\brief Alter the contact normal of a specific contact point in the set.

	\note Changing the normal can cause contact points to be ignored.
	\note This must be a normalized vector.

	@see PxModifiableContact.normal
	*/
	PX_FORCE_INLINE		void setNormal(PxU32 i, const PxVec3& n)		
	{ 
		mContacts[i].normal = n;
	}

	/**
	\brief Get the separation of a specific contact point in the set.

	This value can be either positive or negative. A negative value denotes penetration whereas a positive value denotes separation.

	@see PxModifiableContact.separation
	*/
	PX_FORCE_INLINE		PxReal getSeparation(PxU32 i) const				{ return mContacts[i].separation;	}

	/**
	\brief Alter the separation of a specific contact point in the set.

	@see PxModifiableContact.separation
	*/
	PX_FORCE_INLINE		void setSeparation(PxU32 i, PxReal s)			
	{
		mContacts[i].separation = s; 
	}

	/**
	\brief Get the target velocity of a specific contact point in the set.

	@see PxModifiableContact.targetVelocity

	*/
	PX_FORCE_INLINE		const PxVec3& getTargetVelocity(PxU32 i) const	{ return mContacts[i].targetVel;	}

	/**
	\brief Alter the target velocity of a specific contact point in the set.

	The user-defined target velocity is used to complement the normal and frictional response to a contact. The actual response to a contact depends on 
	the relative velocity, bounce threshold, mass properties and material properties.

	The user-defined property should be defined as a relative velocity in the space (v0 - v1), where v0 is actor[0]'s velocity and v1 is actor[1]'s velocity.

	\note Target velocity can be set in any direction and is independent of the contact normal. Any component of the target velocity that projects onto the contact normal
	will affect normal response and may cause the bodies to either suck into each-other or separate. Any component of the target velocity that does not project onto the contact
	normal will affect the friction response. Target velocities tangential to the contact normal can be an effective way of replicating effects like a conveyor belt.

	@see PxModifiableContact.targetVelocity
	*/
	PX_FORCE_INLINE		void setTargetVelocity(PxU32 i, const PxVec3& v)
	{
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->flags |= PxContactHeader::eHAS_TARGET_VELOCITY;
		mContacts[i].targetVel = v;
	}

	/**
	\brief Get the face index with respect to the first shape of the pair for a specific contact point in the set.

	@see PxModifiableContact.internalFaceIndex0
	*/
	PX_FORCE_INLINE		PxU32 getInternalFaceIndex0(PxU32 i)			{ return mContacts[i].internalFaceIndex0; }

	/**
	\brief Get the face index with respect to the second shape of the pair for a specific contact point in the set.

	@see PxModifiableContact.internalFaceIndex1
	*/
	PX_FORCE_INLINE		PxU32 getInternalFaceIndex1(PxU32 i)			{ return mContacts[i].internalFaceIndex1; }

	/**
	\brief Get the maximum impulse for a specific contact point in the set.

	The value of maxImpulse is a real value in the range [0, PX_MAX_F32]. A value of 0 will disable the contact. The applied impulse will be clamped such that it
	cannot exceed the max impulse.

	@see PxModifiableContact.maxImpulse
	*/
	PX_FORCE_INLINE		PxReal getMaxImpulse(PxU32 i) const				{ return mContacts[i].maxImpulse;	}

	/**
	\brief Alter the maximum impulse for a specific contact point in the set.

	\note Must be nonnegative. If set to zero, the contact point will be ignored, otherwise the impulse applied inside the solver will be clamped such that it cannot
	exceed this value.

	@see PxModifiableContact.maxImpulse
	*/
	PX_FORCE_INLINE		void setMaxImpulse(PxU32 i, PxReal s)			
	{
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->flags |= PxContactHeader::eHAS_MAX_IMPULSE;
		mContacts[i].maxImpulse = s; 
	}

	/**
	\brief Ignore the contact point.

	If a contact point is ignored then no force will get applied at this point. This can be used to disable collision in certain areas of a shape, for example.
	*/
	PX_FORCE_INLINE		void ignore(PxU32 i)							{ mContacts[i].maxImpulse = 0.f; }

	/**
	\brief The number of contact points in the set.
	*/
	PX_FORCE_INLINE		PxU32 size() const								{ return mCount; }

	/**
	\brief Returns the invMassScale of body 0

	The scale is defaulted to 1.0, meaning that the body's true mass will be used.
	*/
	PX_FORCE_INLINE		PxReal getInvMassScale0() const					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		return header->invMassScale0;
	}

	/**
	\brief Returns the invMassScale of body 1

	The scale is defaulted to 1.0, meaning that the body's true mass will be used.
	*/
	PX_FORCE_INLINE		PxReal getInvMassScale1() const					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		return header->invMassScale1;
	}

	/**
	\brief Returns the invInertiaScale of body 0

	The scale is defaulted to 1.0, meaning that the body's true invInertia will be used.
	*/
	PX_FORCE_INLINE		PxReal getInvInertiaScale0() const					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		return header->invInertiaScale0;
	}

	/**
	\brief Returns the invInertiaScale of body 1

	The scale is defaulted to 1.0, meaning that the body's true invInertia will be used.
	*/
	PX_FORCE_INLINE		PxReal getInvInertiaScale1() const					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		return header->invInertiaScale1;
	}

	/**
	\brief Sets the invMassScale of body 0

	This can be set to any value in the range [0, PX_MAX_F32). A value < 1.0 makes this contact treat the body as if it had larger mass. A value of 0.f makes this contact
	treat the body as if it had infinite mass. Any value > 1.f makes this contact treat the body as if it had smaller mass.
	*/
	PX_FORCE_INLINE		void setInvMassScale0(const PxReal scale)					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->invMassScale0 = scale;
		header->flags |= PxContactHeader::eHAS_MODIFIED_MASS_RATIOS;
	}

	/**
	\brief Sets the invMassScale of body 1

	This can be set to any value in the range [0, PX_MAX_F32). A value < 1.0 makes this contact treat the body as if it had larger mass. A value of 0.f makes this contact
	treat the body as if it had infinite mass. Any value > 1.f makes this contact treat the body as if it had smaller mass.
	*/
	PX_FORCE_INLINE		void setInvMassScale1(const PxReal scale)					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->invMassScale1 = scale;
		header->flags |= PxContactHeader::eHAS_MODIFIED_MASS_RATIOS;
	}

	/**
	\brief Sets the invInertiaScale of body 0

	This can be set to any value in the range [0, PX_MAX_F32). A value < 1.0 makes this contact treat the body as if it had larger inertia. A value of 0.f makes this contact
	treat the body as if it had infinite inertia. Any value > 1.f makes this contact treat the body as if it had smaller inertia.
	*/
	PX_FORCE_INLINE		void setInvInertiaScale0(const PxReal scale)					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->invInertiaScale0 = scale;
		header->flags |= PxContactHeader::eHAS_MODIFIED_MASS_RATIOS;
	}

	/**
	\brief Sets the invInertiaScale of body 1

	This can be set to any value in the range [0, PX_MAX_F32). A value < 1.0 makes this contact treat the body as if it had larger inertia. A value of 0.f makes this contact
	treat the body as if it had infinite inertia. Any value > 1.f makes this contact treat the body as if it had smaller inertia.
	*/
	PX_FORCE_INLINE		void setInvInertiaScale1(const PxReal scale)					
	{ 
		const size_t headerOffset = sizeof(PxModifyContactHeader) +  sizeof(PxContactPatchBase);
		PxModifyContactHeader* header = reinterpret_cast<PxModifyContactHeader*>(reinterpret_cast<PxU8*>(mContacts) - headerOffset);
		header->invInertiaScale1 = scale;
		header->flags |= PxContactHeader::eHAS_MODIFIED_MASS_RATIOS;
	}

protected:
	PxU32					mCount;			//!< Number of contact points in the set
	PxModifiableContact*	mContacts;		//!< The contact points of the set
};



/**
\brief An array of instances of this class is passed to PxContactModifyCallback::onContactModify().

@see PxContactModifyCallback
*/

class PxContactModifyPair
{
public:

	/**
	\brief The actors which make up the pair in contact. 
	
	Note that these are the actors as seen by the simulation, and may have been deleted since the simulation step started.
	*/

	const PxRigidActor*		actor[2];
	/**
	\brief The shapes which make up the pair in contact. 
	
	Note that these are the shapes as seen by the simulation, and may have been deleted since the simulation step started.
	*/
	
	const PxShape*			shape[2];

	/**
	\brief The shape to world transforms of the two shapes. 
	
	These are the transforms as the simulation engine sees them, and may have been modified by the application
	since the simulation step started.
	
	*/

	PxTransform 			transform[2];

	/**
	\brief An array of contact points between these two shapes.
	*/

	PxContactSet			contacts;
};


/**
\brief An interface class that the user can implement in order to modify contact constraints.

<b>Threading:</b> It is <b>necessary</b> to make this class thread safe as it will be called in the context of the
simulation thread. It might also be necessary to make it reentrant, since some calls can be made by multi-threaded
parts of the physics engine.

You can enable the use of this contact modification callback by raising the flag PxPairFlag::eMODIFY_CONTACTS in
the filter shader/callback (see #PxSimulationFilterShader) for a pair of rigid body objects.

Please note: 
+ Raising the contact modification flag will not wake the actors up automatically.
+ It is not possible to turn off the performance degradation by simply removing the callback from the scene, the
  filter shader/callback has to be used to clear the contact modification flag.
+ The contacts will only be reported as long as the actors are awake. There will be no callbacks while the actors are sleeping.

@see PxScene.setContactModifyCallback() PxScene.getContactModifyCallback()
*/
class PxContactModifyCallback
{
public:

	/**
	\brief Passes modifiable arrays of contacts to the application.

	The initial contacts are as determined fresh each frame by collision detection.
	
	The number of contacts can not be changed, so you cannot add your own contacts.  You may however
	disable contacts using PxContactSet::ignore().

	@see PxContactModifyPair
	*/
	virtual void onContactModify(PxContactModifyPair* const pairs, PxU32 count) = 0;

protected:
	virtual ~PxContactModifyCallback(){}
};

/**
\brief An interface class that the user can implement in order to modify CCD contact constraints.

<b>Threading:</b> It is <b>necessary</b> to make this class thread safe as it will be called in the context of the
simulation thread. It might also be necessary to make it reentrant, since some calls can be made by multi-threaded
parts of the physics engine.

You can enable the use of this contact modification callback by raising the flag PxPairFlag::eMODIFY_CONTACTS in
the filter shader/callback (see #PxSimulationFilterShader) for a pair of rigid body objects.

Please note: 
+ Raising the contact modification flag will not wake the actors up automatically.
+ It is not possible to turn off the performance degradation by simply removing the callback from the scene, the
  filter shader/callback has to be used to clear the contact modification flag.
+ The contacts will only be reported as long as the actors are awake. There will be no callbacks while the actors are sleeping.

@see PxScene.setContactModifyCallback() PxScene.getContactModifyCallback()
*/
class PxCCDContactModifyCallback
{
public:

	/**
	\brief Passes modifiable arrays of contacts to the application.

	The initial contacts are as determined fresh each frame by collision detection.
	
	The number of contacts can not be changed, so you cannot add your own contacts.  You may however
	disable contacts using PxContactSet::ignore().

	@see PxContactModifyPair
	*/
	virtual void onCCDContactModify(PxContactModifyPair* const pairs, PxU32 count) = 0;

protected:
	virtual ~PxCCDContactModifyCallback(){}
};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

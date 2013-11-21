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


#ifndef PX_CONTACT_MODIFY_CALLBACK
#define PX_CONTACT_MODIFY_CALLBACK
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "PxShape.h"
#include "PxContact.h"
#include "foundation/PxTransform.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxShape;

/**
\brief per-ontact point solver inputs as used by contact modification
*/

struct PxContactPointAux
{
	/**
	\brief The desired target velocity at the contact point in the direction of the contact normal.  For resting contact, this is zero, or positive
	to make the objects pop apart.
	*/
	PxVec3	targetVelocity;

	/**
	\brief The maximum impulse the solver is permitted to apply at the contact point in order to try to meet the 
	targetVelocity requirement.  If max impulse is zero, the solver is not able to apply any impulse at the contact, and as a result the contact will be ignored.
	*/
	PxReal	maxImpulse;
};

/**
\brief An array of contact points, as passed to contact modification.

The word 'set' in the name does not imply that duplicates are filtered in any 
way.  This initial set of contacts does potentially get reduced to a smaller 
set before being passed to the solver.

You can use the accessors to read and write contact properties.  The number of 
contacts is immutable, other than being able to disable contacts using ignore().

@see PxContactModifyCallback, PxContactPoint, PxContactPointAux
*/
class PxContactSet
{
public:
	PX_FORCE_INLINE		const PxVec3& getPoint(PxU32 i) const			{ return mContacts[i].point;		}
	PX_FORCE_INLINE		void setPoint(PxU32 i, const PxVec3& p)			{ mContacts[i].point = p; }

	PX_FORCE_INLINE		const PxVec3& getNormal(PxU32 i) const			{ return mContacts[i].normal;	}
	PX_FORCE_INLINE		void setNormal(PxU32 i, const PxVec3& n)		{ mContacts[i].normal = n;}

	PX_FORCE_INLINE		PxReal getSeparation(PxU32 i) const				{ return mContacts[i].separation;	}
	PX_FORCE_INLINE		void setSeparation(PxU32 i, PxReal s)			{ mContacts[i].separation = s; }

	PX_FORCE_INLINE		const PxVec3& getTargetVelocity(PxU32 i) const	{ return mAux[i].targetVelocity;	}
	PX_FORCE_INLINE		void setTargetVelocity(PxU32 i, const PxVec3& v){ mAux[i].targetVelocity = v;}

	PX_FORCE_INLINE		PxU32 getInternalFaceIndex0(PxU32 i)			{ return mContacts[i].internalFaceIndex0; }
	PX_FORCE_INLINE		PxU32 getInternalFaceIndex1(PxU32 i)			{ return mContacts[i].internalFaceIndex1; }


	// maxImpulse must be nonnegative. Set to zero to ignore this contact

	PX_FORCE_INLINE		PxReal getMaxImpulse(PxU32 i) const				{ return mAux[i].maxImpulse;	}
	PX_FORCE_INLINE		void setMaxImpulse(PxU32 i, PxReal s)			{ mAux[i].maxImpulse = s; }

	PX_FORCE_INLINE		void ignore(PxU32 i)							{ mAux[i].maxImpulse = 0; }
	PX_FORCE_INLINE		PxU32 size() const								{ return mCount; }

protected:
	PxU32					mCount;
	PxContactPoint*			mContacts;
	PxContactPointAux*		mAux;
};



/**
\brief An array of instances of this class is passed to PxContactModifyCallback::onContactModify().

@see PxContactModifyCallback
*/

class PxContactModifyPair
{
public:
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

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

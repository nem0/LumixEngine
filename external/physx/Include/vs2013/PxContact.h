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


#ifndef PX_CONTACT_H
#define PX_CONTACT_H

#include "foundation/PxVec3.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

#ifdef PX_VC
#pragma warning(push)
#pragma warning(disable: 4324)	// Padding was added at the end of a structure because of a __declspec(align) value.
#endif

#define PXC_CONTACT_NO_FACE_INDEX 0xffffffff


/**
\brief Base header structure for compressed contact data.
*/
struct PxContactHeader
{
	enum PxContactHeaderFlags
	{
		eHAS_FACE_INDICES = 1,				//!< Indicates this contact stream has face indices.
		eMODIFIABLE = 2,					//!< Indicates this contact stream is modifiable.
		eFORCE_NO_RESPONSE = 4,				//!< Indicates this contact stream is notify-only (no contact response).
		eHAS_MODIFIED_MASS_RATIOS = 8,		//!< Indicates this contact stream has modified mass ratios
		eHAS_TARGET_VELOCITY = 16,			//!< Indicates this contact stream has target velocities set
		eHAS_MAX_IMPULSE = 32				//!< Indicates this contact stream has max impulses set
	};
	/**
	\brief Total contact count for entire compressed contact stream
	*/
	PxU16 totalContactCount;		//2
	/**
	\brief The flags
	@see PxContactHeaderFlags
	*/
	PxU16 flags;					//4
};

/**
\brief Extended header structure for modifiable contacts.
*/
struct PxModifyContactHeader : public PxContactHeader
{
	/**
	\brief Inverse mass scale for body A.
	*/
	PxReal invMassScale0;			//8
	/**
	\brief Inverse mass scale for body B.
	*/
	PxReal invMassScale1;			//12
	/**
	\brief Inverse inertia scale for body A.
	*/
	PxReal invInertiaScale0;		//16
	/**
	\brief Inverse inertia scale for body B.
	*/
	PxReal invInertiaScale1;		//20
};

/**
\brief Base header for a contact patch
*/
struct PxContactPatchBase
{
	/**
	\brief Number of contacts in this patch.
	*/
	PxU16	nbContacts;				//2
	/**
	\brief Flags for this patch.
	*/
	PxU16	flags;					//4
};

/**
\brief Header for contact patch where all points share same material and normal
*/
struct PxContactPatch : public PxContactPatchBase
{
	/**
	\brief Contact normal
	*/
	PxVec3	normal;					//16
	/**
	\brief Static friction coefficient
	*/
	PxReal	staticFriction;			//20
	/**
	\brief Dynamic friction coefficient
	*/
	PxReal	dynamicFriction;		//24
	/**
	\brief Restitution coefficient
	*/
	PxReal	restitution;			//28
	/**
	\brief Shape A's material index
	*/
	PxU16	materialIndex0;			//30
	/**
	\brief Shape B's material index
	*/
	PxU16	materialIndex1;			//32
};

/**
\brief Base contact point data
*/
struct PxSimpleContact
{
	/**
	\brief Contact point in world space
	*/
	PxVec3	contact;				//12
	/**
	\brief Separation value (negative implies penetration).
	*/
	PxReal	separation;				//16
};

/**
\brief Extended contact point data including face (feature) indices
*/
struct PxFeatureContact : public PxSimpleContact
{	
	/**
	\brief Face index on shape A.
	*/
	PxU32	internalFaceIndex0;		//20
	/**
	\brief Face index on shape B.
	*/
	PxU32	internalFaceIndex1;		//24
};

/**
\brief A modifiable contact point. This has additional fields per-contact to permit modification by user.
\note Not all fields are currently exposed to the user.
*/
struct PxModifiableContact : public PxFeatureContact
{
	/**
	\brief Contact normal
	*/
	PxVec3	normal;					//36
	/**
	\brief Target velocity
	*/
	PxVec3	targetVel;				//48
	/**
	\brief Maximum impulse
	*/
	PxReal	maxImpulse;				//52
	/**
	\brief Static friction coefficient
	*/
	PxReal	staticFriction;			//56
	/**
	\brief Dynamic friction coefficient
	*/
	PxReal	dynamicFriction;		//60
	/**
	\brief Restitution coefficient
	*/
	PxReal	restitution;			//64
	/**
	\brief Material index on shape A
	*/
	PxU16	materialIndex0;			//66
	/**
	\brief Material index on shape B
	*/
	PxU16	materialIndex1;			//68
	/**
	\brief Flags
	*/
	PxU32	flags;					//72
};

/**
\brief A class to iterate over a compressed contact stream. This supports read-only access to the various contact formats.
*/
struct PxContactStreamIterator
{
	/**
	\brief Utility zero vector to optimize functions returning zero vectors when a certain flag isn't set.
	\note This allows us to return by reference instead of having to return by value. Returning by value will go via memory (registers -> stack -> registers), which can 
	cause performance issues on certain platforms.
	*/
	PxVec3 zero;
	/**
	\brief The current contact header.
	*/
	const PxContactHeader* header;
	/**
	\brief Current pointer in the stream.
	*/
	const PxU8* currPtr;
	/**
	\brief Pointer to the end of the stream.
	*/
	const PxU8* endPtr;
	/**
	\brief Pointer to the start of the patch.
	*/
	const PxU8* patchStart;
	/**
	\brief Pointer to the end of the patch.
	*/
	const PxU8* patchEnd;
	/**
	\brief Pointer to the first contact in the patch.
	*/
	const PxSimpleContact* contactStart;
	/**
	\brief Size of the stream in bytes.
	*/
	PxU32 streamSize;
	/**
	\brief Total number of contacts in the patch.
	*/
	PxU32 nbContactsInPatch;
	/**
	\brief Current contact index in the patch
	*/
	PxU32 currentContact;
	/**
	\brief Size of contact patch header 
	\note This varies whether the patch is modifiable or not.
	*/
	PxU32 contactPatchHeaderSize;
	/**
	\brief Contact point size
	\note This varies whether the patch has feature indices or is modifiable.
	*/
	PxU32 contactPointSize;
	/**
	\brief Indicates whether this stream carries face indices
	*/
	PxU32 hasFaceIndices;
	/**
	\brief Indicates whether this stream is created from modifiable contact (internal usage), the variables are still read-only
	*/
	PxU32 contactsWereModifiable;
	/**
	\brief Indicates whether this stream is notify-only or not.
	*/
	PxU32 forceNoResponse;

	/**
	\brief Constructor
	\param[in] stream Pointer to the start of the stream.
	\param[in] size Size of the stream in bytes.
	*/
	PX_FORCE_INLINE PxContactStreamIterator(const PxU8* stream, PxU32 size) 
		: zero(0.f), streamSize(size), nbContactsInPatch(0), currentContact(0)
	{		
		const PxContactHeader* h = reinterpret_cast<const PxContactHeader*>(stream);
		header = h;		

		bool modify = false;
		bool faceIndices = false;
		bool response = false;

		PxU32 pointSize = 0;
		PxU32 patchHeaderSize = 0;
		const PxU8* start = NULL;

		if(size > 0)
		{
			modify = (h->flags & PxContactHeader::eMODIFIABLE) != 0;
			faceIndices = (h->flags & PxContactHeader::eHAS_FACE_INDICES) != 0;

			start = stream + (modify ? sizeof(PxModifyContactHeader) : sizeof(PxContactHeader));


			PX_ASSERT(((PxU32)(start - stream)) < size);
			//if(((PxU32)(start - stream)) < size)
			{
				patchHeaderSize = modify ? sizeof(PxContactPatchBase) : sizeof(PxContactPatch);
				pointSize = modify ?  sizeof(PxModifiableContact) : faceIndices ? sizeof(PxFeatureContact) : sizeof(PxSimpleContact);

				response = (header->flags & PxContactHeader::eFORCE_NO_RESPONSE) == 0;
			}
		}

		contactsWereModifiable = (PxU32)modify;
		hasFaceIndices = (PxU32)faceIndices;
		forceNoResponse = (PxU32)!response;

		contactPatchHeaderSize = patchHeaderSize;
		contactPointSize = pointSize;

		patchStart = start;
		patchEnd = start;
		currPtr = start;
	}

	/**
	\brief Returns whether there are more patches in this stream.
	\return Whether there are more patches in this stream.
	*/
	PX_FORCE_INLINE bool hasNextPatch() const
	{
		return ((PxU32)(patchEnd - reinterpret_cast<const PxU8*>(header))) < streamSize;
	}

	/**
	\brief Returns the total contact count.
	\return Total contact count.
	*/
	PX_FORCE_INLINE PxU32 getTotalContactCount() const
	{
		return header->totalContactCount;
	}

	/**
	\brief Advances iterator to next contact patch.
	*/
	PX_INLINE void nextPatch()
	{
		const PxU8* start = patchEnd;
		patchStart = start;

		if(((PxU32)(start - (reinterpret_cast<const PxU8*>(header)))) < streamSize)
		{
			const PxU32 numContactsInPatch = *(reinterpret_cast<const PxU16*>(patchStart));
			nbContactsInPatch = numContactsInPatch;

			patchEnd = start + contactPatchHeaderSize + numContactsInPatch * contactPointSize;
			currPtr = start + contactPatchHeaderSize;
			currentContact = 0;
		}
		else
		{
			patchEnd = start;
		}
	}

	/**
	\brief Returns if the current patch has more contacts.
	\return If there are more contacts in the current patch.
	*/
	PX_FORCE_INLINE bool hasNextContact() const
	{
		return currentContact < nbContactsInPatch;
	}

	/**
	\brief Advances to the next contact in the patch.
	*/
	PX_FORCE_INLINE void nextContact()
	{
		PX_ASSERT(currentContact < nbContactsInPatch);
		currentContact++;
		contactStart = reinterpret_cast<const PxSimpleContact*>(currPtr);
		currPtr += contactPointSize;
	}

	/**
	\brief Gets the current contact's normal
	\return The current contact's normal.
	*/
	PX_FORCE_INLINE const PxVec3& getContactNormal() const
	{
		return contactsWereModifiable ? getModifiableContact().normal : getContactPatch().normal;
	}

	/**
	\brief Gets the inverse mass scale for body 0.
	\return The inverse mass scale for body 0.
	*/
	PX_FORCE_INLINE PxReal getInvMassScale0() const
	{
		return contactsWereModifiable ? getModifiableContactHeader().invMassScale0 : 1.f;
	}

	/**
	\brief Gets the inverse mass scale for body 1.
	\return The inverse mass scale for body 1.
	*/
	PX_FORCE_INLINE PxReal getInvMassScale1() const
	{
		return contactsWereModifiable ? getModifiableContactHeader().invMassScale1 : 1.f;
	}

	/**
	\brief Gets the inverse inertia scale for body 0.
	\return The inverse inertia scale for body 0.
	*/
	PX_FORCE_INLINE PxReal getInvInertiaScale0() const
	{
		return contactsWereModifiable ? getModifiableContactHeader().invInertiaScale0 : 1.f;
	}

	/**
	\brief Gets the inverse inertia scale for body 1.
	\return The inverse inertia scale for body 1.
	*/
	PX_FORCE_INLINE PxReal getInvInertiaScale1() const
	{
		return contactsWereModifiable ? getModifiableContactHeader().invInertiaScale1 : 1.f;
	}

	/**
	\brief Gets the contact's max impulse.
	\return The contact's max impulse.
	*/
	PX_FORCE_INLINE PxReal getMaxImpulse() const
	{
		return contactsWereModifiable ? getModifiableContact().maxImpulse : PX_MAX_REAL;
	}

	/**
	\brief Gets the contact's target velocity.
	\return The contact's target velocity.
	*/
	PX_FORCE_INLINE const PxVec3& getTargetVel() const
	{
		return contactsWereModifiable ? getModifiableContact().targetVel : zero;
	}

	/**
	\brief Gets the contact's contact point.
	\return The contact's contact point.
	*/
	PX_FORCE_INLINE const PxVec3& getContactPoint() const
	{
		return contactStart->contact;
	}

	/**
	\brief Gets the contact's separation.
	\return The contact's separation.
	*/
	PX_FORCE_INLINE PxReal getSeparation() const
	{
		return contactStart->separation;
	}

	/**
	\brief Gets the contact's face index for shape 0.
	\return The contact's face index for shape 0.
	*/
	PX_FORCE_INLINE PxU32 getFaceIndex0() const
	{
		return hasFaceIndices ? (static_cast<const PxFeatureContact*>(contactStart))->internalFaceIndex0 : PXC_CONTACT_NO_FACE_INDEX;
	}

	/**
	\brief Gets the contact's face index for shape 1.
	\return The contact's face index for shape 1.
	*/
	PX_FORCE_INLINE PxU32 getFaceIndex1() const
	{
		return hasFaceIndices ? (static_cast<const PxFeatureContact*>(contactStart))->internalFaceIndex1 : PXC_CONTACT_NO_FACE_INDEX;
	}

	/**
	\brief Gets the contact's static friction coefficient.
	\return The contact's static friction coefficient.
	*/
	PX_FORCE_INLINE PxReal getStaticFriction() const
	{
		return contactsWereModifiable ? getModifiableContact().staticFriction : getContactPatch().staticFriction;
	}

	/**
	\brief Gets the contact's static dynamic coefficient.
	\return The contact's static dynamic coefficient.
	*/
	PX_FORCE_INLINE PxReal getDynamicFriction() const
	{
		return contactsWereModifiable ? getModifiableContact().dynamicFriction : getContactPatch().dynamicFriction;
	}

	/**
	\brief Gets the contact's restitution coefficient.
	\return The contact's restitution coefficient.
	*/
	PX_FORCE_INLINE PxReal getRestitution() const
	{
		return contactsWereModifiable ? getModifiableContact().restitution : getContactPatch().restitution;
	}

	/**
	\brief Gets the contact's material flags.
	\return The contact's material flags.
	*/
	PX_FORCE_INLINE PxU32 getMaterialFlags() const
	{
		return contactsWereModifiable ? getModifiableContact().flags : getContactPatch().flags;
	}

	/**
	\brief Gets the contact's material index for shape 0.
	\return The contact's material index for shape 0.
	*/
	PX_FORCE_INLINE PxU16 getMaterialIndex0() const
	{
		return contactsWereModifiable ? getModifiableContact().materialIndex0 : getContactPatch().materialIndex0;
	}

	/**
	\brief Gets the contact's material index for shape 1.
	\return The contact's material index for shape 1.
	*/
	PX_FORCE_INLINE PxU16 getMaterialIndex1() const
	{
		return contactsWereModifiable ? getModifiableContact().materialIndex1 : getContactPatch().materialIndex1;
	}

	/**
	\brief Advances the contact stream iterator to a specific contact index.
	*/
	bool advanceToIndex(const PxU32 initialIndex)
	{
		PX_ASSERT(currPtr == (reinterpret_cast<const PxU8*>(header + 1)));
	
		PxU32 numToAdvance = initialIndex;

		if(numToAdvance == 0)
		{
			PX_ASSERT(hasNextPatch());
			nextPatch();
			return true;
		}
		
		while(numToAdvance)
		{
			while(hasNextPatch())
			{
				nextPatch();
				PxU32 patchSize = nbContactsInPatch;
				if(numToAdvance <= patchSize)
				{
					while(hasNextContact())
					{
						--numToAdvance;
						if(numToAdvance == 0)
							return true;
						nextContact();
					}
				}
				else
				{
					numToAdvance -= patchSize;
				}
			}
		}
		return false;
	}

private:

	/**
	\brief Internal helper
	*/
	PX_FORCE_INLINE const PxContactPatch& getContactPatch() const
	{
		PX_ASSERT(!contactsWereModifiable);
		return *reinterpret_cast<const PxContactPatch*>(patchStart);
	}

	/**
	\brief Internal helper
	*/
	PX_FORCE_INLINE const PxModifiableContact& getModifiableContact() const
	{
		PX_ASSERT(contactsWereModifiable);
		return *static_cast<const PxModifiableContact*>(contactStart);
	}

	/**
	\brief Internal helper
	*/
	PX_FORCE_INLINE const PxModifyContactHeader& getModifiableContactHeader() const
	{
		PX_ASSERT(contactsWereModifiable);
		return *static_cast<const PxModifyContactHeader*>(header);
	}

};


#ifdef PX_VC
#pragma warning(pop)
#endif

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif

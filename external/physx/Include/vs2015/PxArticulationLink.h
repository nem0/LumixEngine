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


#ifndef PX_PHYSICS_NX_ARTICULATION_LINK
#define PX_PHYSICS_NX_ARTICULATION_LINK
/** \addtogroup physics 
@{ */

#include "PxPhysXConfig.h"
#include "PxArticulationJoint.h"
#include "PxRigidBody.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief a component of an articulation that represents a rigid body

A limited subset of the properties of PxRigidDynamic are supported. In particular, sleep properties
are attributes of the articulation rather than each individual body, damping and velocity limits
are not supported, and links may not be kinematic.

@see PxArticulation PxArticulation.createLink PxArticulationJoint PxRigidBody
*/

class PxArticulationLink : public PxRigidBody
{
public:
	/**
	\brief Deletes the articulation link.
	
	\note Only a leaf articulation link can be released
	
	Do not keep a reference to the deleted instance.

	@see PxArticulation::createLink()
	*/
	virtual		void			release() = 0;


	/**
	\brief get the articulation to which this articulation link belongs

	\return the articulation to which this link belongs
	*/
	virtual		PxArticulation&	getArticulation() const = 0;

	/**
	\brief Get the joint which connects this link to its parent.
	
	\return The joint connecting the link to the parent. NULL for the root link.

	@see PxArticulationJoint
	*/
	virtual		PxArticulationJoint*	getInboundJoint() const = 0;

	/**
	\brief Get number of child links.

	\return the number of child links

	@see getChildren()
	*/
	virtual		PxU32			getNbChildren() const = 0;

	/**
	\brief Retrieve all the child links.

	\param[out] userBuffer The buffer to receive articulation link pointers.
	\param[in] bufferSize Size of provided user buffer.
	\return Number of articulation links written to the buffer.

	@see getNbChildren()
	*/
	virtual		PxU32			getChildren(PxArticulationLink** userBuffer, PxU32 bufferSize) const = 0;

	virtual		const char*		getConcreteTypeName() const					{	return "PxArticulationLink"; }

protected:
	PX_INLINE					PxArticulationLink(PxType concreteType, PxBaseFlags baseFlags) : PxRigidBody(concreteType, baseFlags) {}
	PX_INLINE					PxArticulationLink(PxBaseFlags baseFlags) : PxRigidBody(baseFlags)	{}
	virtual						~PxArticulationLink()	{}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxArticulationLink", name) || PxRigidBody::isKindOf(name);		}
};

PX_DEPRECATED PX_INLINE PxArticulationLink*			PxActor::isArticulationLink()			{ return is<PxArticulationLink>();	}
PX_DEPRECATED PX_INLINE const PxArticulationLink*	PxActor::isArticulationLink()	const	{ return is<PxArticulationLink>();	}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

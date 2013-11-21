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


#ifndef PX_PHYSICS_NXMATERIAL
#define PX_PHYSICS_NXMATERIAL
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "PxMaterialFlags.h"
#include "common/PxSerialFramework.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxScene;

class PxMaterial : public PxSerializable
{
public:

	/**
	\brief Deletes the material.
	
	\note This will decrease the reference count by one.

	Releases the application's reference to the material.
	The material is destroyed when the application's reference is released and all shapes referencing the material are destroyed.

	@see PxScene::createMaterial()
	*/
	virtual		void			release() = 0;

	/**
	\brief Returns the reference count of the material.

	At creation, the reference count of the material is 1. Every shape referencing this material increments the
	count by 1.	When the reference count reaches 0, and only then, the material gets destroyed automatically.

	\return the current reference count.
	*/
	virtual PxU32				getReferenceCount() const = 0;

	/**
	\brief Sets the coefficient of dynamic friction.
	
	The coefficient of dynamic friction should be in [0, +inf]. If set to greater than staticFriction, the effective value of staticFriction will be increased to match.

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] coef Coefficient of dynamic friction. <b>Range:</b> [0, +inf]

	@see getDynamicFriction()
	*/
	virtual		void			setDynamicFriction(PxReal coef) = 0;

	/**
	\brief Retrieves the DynamicFriction value.

	\return The coefficient of dynamic friction.

	@see setDynamicFriction
	*/
	virtual		PxReal			getDynamicFriction() const = 0;

	/**
	\brief Sets the coefficient of static friction
	
	The coefficient of static friction should be in the range [0, +inf]

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] coef Coefficient of static friction. <b>Range:</b> [0,inf]

	@see getStaticFriction() 
	*/
	virtual		void			setStaticFriction(PxReal coef) = 0;

	/**
	\brief Retrieves the coefficient of static friction.
	\return The coefficient of static friction.

	@see setStaticFriction 
	*/
	virtual		PxReal			getStaticFriction() const = 0;

	/**
	\brief Sets the coefficient of restitution 
	
	A coefficient of 0 makes the object bounce as little as possible, higher values up to 1.0 result in more bounce.

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] rest Coefficient of restitution. <b>Range:</b> [0,1]

	@see getRestitution() 
	*/
	virtual		void			setRestitution(PxReal rest) = 0;

	/**
	\brief Retrieves the coefficient of restitution.     

	See #setRestitution.

	\return The coefficient of restitution.

	@see setRestitution() 
	*/
	virtual		PxReal			getRestitution() const = 0;

	/**
	\brief Raises or clears a particular material flag.
	
	See the list of flags #PxMaterialFlag

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] flag The PxMaterial flag to raise(set) or clear.

	@see getFlags() PxMaterialFlag
	*/
	virtual		void			setFlag(PxMaterialFlag::Enum flag, bool) = 0;


	/**
	\brief sets all the material flags.
	
	See the list of flags #PxMaterialFlag

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	*/
	virtual		void 			setFlags( PxMaterialFlags inFlags ) = 0;

	/**
	\brief Retrieves the flags. See #PxMaterialFlag.

	\return The material flags.

	@see PxMaterialFlag setFlags()
	*/
	virtual		PxMaterialFlags	getFlags() const = 0;

	/**
	\brief Sets the friction combine mode.
	
	See the enum ::PxCombineMode .

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] combMode Friction combine mode to set for this material. See #PxCombineMode.

	@see PxCombineMode getFrictionCombineMode setStaticFriction() setDynamicFriction()
	*/
	virtual		void			setFrictionCombineMode(PxCombineMode::Enum combMode) = 0;

	/**
	\brief Retrieves the friction combine mode.
	
	See #setFrictionCombineMode.

	\return The friction combine mode for this material.

	@see PxCombineMode setFrictionCombineMode() 
	*/
	virtual		PxCombineMode::Enum	getFrictionCombineMode() const = 0;

	/**
	\brief Sets the restitution combine mode.
	
	See the enum ::PxCombineMode .

	<b>Sleeping:</b> Does <b>NOT</b> wake any actors which may be affected.

	\param[in] combMode Restitution combine mode for this material. See #PxCombineMode.

	@see PxCombineMode getRestitutionCombineMode() setRestitution()
	*/
	virtual		void			setRestitutionCombineMode(PxCombineMode::Enum combMode) = 0;

	/**
	\brief Retrieves the restitution combine mode.
	
	See #setRestitutionCombineMode.

	\return The coefficient of restitution combine mode for this material.

	@see PxCombineMode setRestitutionCombineMode getRestitution()
	*/
	virtual		PxCombineMode::Enum	getRestitutionCombineMode() const = 0;

	//public variables:
				void*			userData;	//!< user can assign this to whatever, usually to create a 1:1 relationship with a user object.

	virtual		const char*		getConcreteTypeName() const					{	return "PxMaterial"; }

protected:
								PxMaterial(PxRefResolver& v) :	PxSerializable(v)	{}
	PX_INLINE					PxMaterial() : userData(NULL)		{}
	virtual						~PxMaterial()						{}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxMaterial", name) || PxSerializable::isKindOf(name); }

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


#ifndef PX_PHYSICS_PX_BASE
#define PX_PHYSICS_PX_BASE

/** \addtogroup common
@{
*/

#include "PxSerialFramework.h"
#include "PxCollection.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

typedef PxU16 PxType;

/**
\brief Flags for PxBase.
*/
struct PxBaseFlag
{	
	enum Enum
	{
		eOWNS_MEMORY			= (1<<0),
		eIS_RELEASABLE			= (1<<1)
	};
};

typedef PxFlags<PxBaseFlag::Enum, PxU16> PxBaseFlags;
PX_FLAGS_OPERATORS(PxBaseFlag::Enum, PxU16)

/**
\brief Base class for objects that can be members of a PxCollection.

All PxBase sub-classes can be serialized.

@see PxCollection 
*/
class PxBase
{
//= ATTENTION! =====================================================================================
// Changing the data layout of this class breaks the binary serialization format.  See comments for 
// PX_BINARY_SERIAL_VERSION.  If a modification is required, please adjust the getBinaryMetaData 
// function.  If the modification is made on a custom branch, please change PX_BINARY_SERIAL_VERSION
// accordingly.
//==================================================================================================
public:
	/**
	\brief Releases the PxBase instance, please check documentation of release in derived class.
	*/
	virtual     void                        release()										= 0;	

	/**
	\brief Returns string name of dynamic type.
	\return	Class name of most derived type of this object.
	*/
	virtual		const char*					getConcreteTypeName() const						{ return NULL; }

	/* brief Implements dynamic cast functionality. 

	Example use:
	
	if(actor->is<PxRigidDynamic>()) {...}

	\return A pointer to the specified type if object matches, otherwise NULL
	*/
	template<class T> T*					is()											{ return typeMatch<T>() ? static_cast<T*>(this) : NULL; }

	/* brief Implements dynamic cast functionality for const objects. 

	Example use:
	
	if(actor->is<PxRigidDynamic>()) {...}

	\return A pointer to the specified type if object matches, otherwise NULL
	*/
	template<class T> const T*				is() const										{ return typeMatch<T>() ? static_cast<const T*>(this) : NULL; }

	/**
	\brief	Returns concrete type of object.
	\return	PxConcreteType::Enum of serialized object

	@see PxConcreteType
	*/
	PX_INLINE	PxType						getConcreteType() const							{ return mConcreteType;	}
				
	/**
	\brief Set PxBaseFlag	

	\param[in] flag The flag to be set
	\param[in] value The flags new value
	*/
	PX_INLINE	void						setBaseFlag(PxBaseFlag::Enum flag, bool value)	{ mBaseFlags = value ? mBaseFlags|flag : mBaseFlags&~flag; }
	
	/**
	\brief Set PxBaseFlags	

	\param[in] inFlags The flags to be set

	@see PxBaseFlags
	*/
	PX_INLINE	void						setBaseFlags(PxBaseFlags inFlags )				{ mBaseFlags = inFlags; }
	
	/**
	\brief Returns PxBaseFlags 

	\return	PxBaseFlags

	@see PxBaseFlags
	*/
	PX_INLINE	PxBaseFlags					getBaseFlags() const							{ return mBaseFlags; }

	/**
	\brief Whether the object is subordinate.
	
	A class is subordinate, if it can only be instantiated in the context of another class.

	\return	Whether the class is subordinate
	
	@see PxSerialization::isSerializable
	*/
	virtual		bool						isReleasable() const							{ return mBaseFlags & PxBaseFlag::eIS_RELEASABLE; }

protected:
	/**
	\brief Constructor setting concrete type and base flags.
	*/
	PX_INLINE								PxBase(PxType concreteType, PxBaseFlags baseFlags)
												: mConcreteType(concreteType), mBaseFlags(baseFlags) {}

	/**
	\brief Deserialization constructor setting base flags.
	*/
	PX_INLINE								PxBase(PxBaseFlags baseFlags) : mBaseFlags(baseFlags) {}

	/**
	\brief Destructor.
	*/
	virtual									~PxBase()										{}

	/**
	\brief Returns whether a given type name matches with the type of this instance
	*/	
	virtual				bool				isKindOf(const char* superClass) const { return !strcmp(superClass, "PxBase"); }

	template<class T>	bool				typeMatch() const
											{
												return PxU32(PxTypeInfo<T>::eFastTypeId)!=PxU32(PxConcreteType::eUNDEFINED) ? 
													PxU32(getConcreteType()) == PxU32(PxTypeInfo<T>::eFastTypeId) : isKindOf(PxTypeInfo<T>::name());
											}


private:
	friend				void				getBinaryMetaData_PxBase(PxOutputStream& stream);

protected:
	PxType									mConcreteType;			// concrete type identifier - see PxConcreteType.
	PxBaseFlags								mBaseFlags;				// internal flags

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

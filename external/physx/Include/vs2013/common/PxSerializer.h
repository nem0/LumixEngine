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


#ifndef PX_SERIALIZER_H
#define PX_SERIALIZER_H
/** \addtogroup extensions
@{
*/

#include "PxSerialFramework.h"
#include "PxCollection.h"
#include "foundation/PxAssert.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/** 
 \brief Serialization interface class.

 PxSerializer is used to extend serializable PxBase classes with serialization functionality. The 
 interface is structured such that per-class adapter instances can be used as opposed to per-object 
 adapter instances, avoiding per object allocations. Hence the methods take a reference to PxBase as a parameter.

 The PxSerializer interface needs to be implemented for binary or RepX serialization to work on custom 
 types. If only RepX serialization is needed, some methods can be left empty, as they are only needed 
 for binary serialization.

 A default implementation is available as a template adapter (PxSerializerDefaultAdapter).

 @see PxSerializerDefaultAdapter, PX_NEW_SERIALIZER_ADAPTER, PxSerializationRegistry::registerSerializer
*/
class PxSerializer
{
public:

	/**********************************************************************************************************************/

	/** @name Basics needed for Binary- and RepX-Serialization
	*/
	//@{
	
	/**
	\brief Returns string name of dynamic type.

	\return	Class name of most derived type of this object.
	*/
	virtual	 const	char*			getConcreteTypeName() const															= 0;   

	/**
	\brief Adds required objects to the collection.
	
	This method does not add the required objects recursively, e.g. objects required by required objects.

	@see PxCollection, PxSerialization::complete
	*/
	virtual			void			requires(PxBase&, PxProcessPxBaseCallback&) const									= 0;
	
	/**
	\brief Whether the object is subordinate.
	
	A class is subordinate, if it can only be instantiated in the context of another class.

	\return	Whether the class is subordinate
	
	@see PxSerialization::isSerializable
	*/
	virtual			bool			isSubordinate() const																= 0;

	//@}
	/**********************************************************************************************************************/

	/**********************************************************************************************************************/

	/** @name Functionality needed for Binary Serialization only
	*/
	//@{

	/**
	\brief Exports object's extra data to stream.
	*/
	virtual         void            exportExtraData(PxBase&, PxSerializationContext&) const								= 0;

	/**
	\brief Exports object's data to stream.
	*/
	virtual         void            exportData(PxBase&, PxSerializationContext&) const									= 0;

	/**
	\brief Register references that the object maintains to other objects.
	*/
	virtual			void			registerReferences(PxBase& obj, PxSerializationContext& s) const					= 0;

	/**
	\brief Returns size needed to create the class instance.

	\return	sizeof class instance.
	*/
	virtual			size_t			getClassSize() const																= 0;

	/**
	\brief Create object at a given address, resolve references and import extra data.

	\param address Location at which object is created. Address is increased by the size of the created object.
	\param context Context for reading external data and resolving references.
	\return	Created PxBase pointer (needs to be identical to address before increment).
	*/
	virtual         PxBase*			createObject(PxU8*& address, PxDeserializationContext& context) const	= 0; 

	//@}
	/**********************************************************************************************************************/
	virtual ~PxSerializer() {}
};


/** 
 \brief Default PxSerializer implementation.
*/
template<class T>
class PxSerializerDefaultAdapter : public PxSerializer
{
public:

	/************************************************************************************************/

	/** @name Basics needed for Binary- and RepX-Serialization
	*/
	//@{

	PxSerializerDefaultAdapter(const char* name) : mTypeName(name){}

	virtual const char* getConcreteTypeName() const
	{ 
		return mTypeName; 
	}

	virtual	void requires(PxBase& obj, PxProcessPxBaseCallback& c) const
	{
		T& t = static_cast<T&>(obj);
		t.requires(c);
	}

	virtual	bool isSubordinate() const
	{
		return false;
	}
		
	//@}
	/************************************************************************************************/

	/** @name Functionality needed for Binary Serialization only
	*/
	//@{

	// object methods

	virtual void exportExtraData(PxBase& obj, PxSerializationContext& s) const
	{ 
		T& t = static_cast<T&>(obj);
		t.exportExtraData(s);
	}

	virtual void exportData(PxBase& obj, PxSerializationContext& s) const
	{ 
		s.writeData(&obj, sizeof(T));
	}

	virtual void registerReferences(PxBase& obj, PxSerializationContext& s) const
	{
		T& t = static_cast<T&>(obj);

		s.registerReference(obj, PX_SERIAL_REF_KIND_PXBASE, size_t(&obj));

		struct RequiresCallback : public PxProcessPxBaseCallback
		{
			RequiresCallback(PxSerializationContext& c) : context(c) {}
			RequiresCallback& operator=(RequiresCallback&) { PX_ASSERT(0); return *this; }
			void process(physx::PxBase& base)
			{				
				context.registerReference(base, PX_SERIAL_REF_KIND_PXBASE, size_t(&base));
			}
			PxSerializationContext& context;
		};

		RequiresCallback callback(s);
		t.requires(callback);	
	}

	// class methods

	virtual size_t getClassSize() const
	{
		return sizeof(T);
	}

	virtual	PxBase*	createObject(PxU8*& address, PxDeserializationContext& context) const
	{
		return T::createObject(address, context);
	}


	//@}
	/************************************************************************************************/

private:
	const char*    mTypeName;	
};

/** 
 \brief Preprocessor Macro to simplify adapter creation.

 Note: that the allocator used for creation needs to match with the one used in PX_DELETE_SERIALIZER_ADAPTER.
*/
#define PX_NEW_SERIALIZER_ADAPTER(x) \
	*new( PxGetFoundation().getAllocatorCallback().allocate(sizeof(PxSerializerDefaultAdapter<x>), \
	"PxSerializerDefaultAdapter",  __FILE__, __LINE__ )) PxSerializerDefaultAdapter<x>(#x)

/** 
 \brief Preprocessor Macro to simplify adapter deletion.
*/
#define PX_DELETE_SERIALIZER_ADAPTER(x) \
	{ PxSerializer* s = x; if (s) { s->~PxSerializer(); PxGetFoundation().getAllocatorCallback().deallocate(s); } }

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

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


//#ifdef REMOVED

#ifndef PX_PHYSICS_COMMON_NX_SERIAL_FRAMEWORK
#define PX_PHYSICS_COMMON_NX_SERIAL_FRAMEWORK

/** \addtogroup common
@{
*/

#include "common/PxPhysXCommon.h"
#include "common/PxTypeInfo.h"
#include "common/PxFields.h"
#include "common/PxFieldDescriptor.h"
#include "foundation/PxFlags.h"


#ifndef PX_DOXYGEN
namespace physx
{
#endif

typedef PxU16 PxType;
class PxSerializable;
class PxOutputStream;
class PxSerialStream;

//! Serialized input data must be aligned to this value
#define PX_SERIAL_FILE_ALIGN	128

//! Objects are written in a fixed order within a serialized file.
struct PxSerialOrder
{
	enum Enum
	{
		eCONVEX			= 20,
		eTRIMESH		= 21,
		eHEIGHTFIELD	= 22,
		eDEFMESH		= 23,
		eCLOTHMESH		= 24,
		eMATERIAL		= 50,
		eSHAPE			= 80,
		eSTATIC			= 81,
		eDYNAMIC		= 82,
		eDEFAULT		= 100,
		eARTICULATION	= 120,
		eJOINT			= 150,
		eCONSTRAINT		= 200,
		eAGGREGATE		= 300,
	};
};

typedef PxU64 PxSerialObjectRef;

struct PxSerialObjectAndRef
{
	PxSerializable*		serializable;
	PxSerialObjectRef	ref;
};

/**
\brief Class used to "resolve pointers" during deserialization.

The ref-resolver remaps pointers to PxSerializable objects within a deserialized memory block.
This class is mainly used by the serialization framework. Users should not have to worry about it.

@see PxSerializable
*/
class PX_PHYSX_COMMON_API PxRefResolver
{
	public:
	virtual					~PxRefResolver()														{}

	/**
	\brief Retrieves new address of deserialized object

	This is called by the serialization framework.

	\param[in]	oldAddress	Old address of serialized object
	\return		New address of serialized object
	*/
	virtual	void*			newAddress(void* oldAddress) const	= 0;

	/**
	\brief Sets new address of deserialized object.

	This is called by the serialization framework.

	\param[in]	oldAddress	Old address of serialized object
	\param[in]	newAddress	New address of serialized object
	*/
	virtual	void			setNewAddress(void* oldAddress, void* newAddress)	= 0;

	/**
	\brief Sets current string table.

	This is called by the serialization framework.

	\param[in]	stringTable	Current string table address
	*/
	virtual	void			setStringTable(const char* stringTable)	= 0;

	/**
	\brief Resolves external reference.

	This is called by the serialization framework.

	\param[in]	name	Name to be resolved
	\return		Resolved name
	*/
	virtual	const char*		resolveName(const char* name)	= 0;
};

/**
\brief Container for user-defined names/references

This is mainly a "link" object accessed by the framework when serializing and
deserializing subsets.

@see PxSerializable
*/
class PX_PHYSX_COMMON_API PxUserReferences
{
	public:
	virtual					~PxUserReferences()														{}


	/** \brief Deprecated alias for getObjectFromRef 

	\param[in]	ref	user-defined reference for this object
	\return		Corresponding object, or NULL if not found
	*/
	PX_DEPRECATED PX_INLINE	PxSerializable*	getObjectFromID(PxSerialObjectRef ref) const			{	return getObjectFromRef(ref); }

	/** \brief Deprecated alias for setObjectRef 
	
	\param[in]	object		Serializable object. See #PxSerializable
	\param[in]	ref			user-defined object reference for this object

	@see setObjectRef
	*/

	PX_DEPRECATED PX_INLINE	void setUserData(PxSerializable* object, PxSerialObjectRef ref)			{	setObjectRef(*object, ref);	}


	/**
	\brief Gets PxSerializable object from its object reference

	This is called by the framework during deserialization, when external references are
	passed to the deserialize function.

	\param[in]	ref	user-defined reference for this object
	\return		Corresponding object, or NULL if not found

	@see PxSerializable
	*/

	virtual	PxSerializable*	getObjectFromRef(PxSerialObjectRef ref) const					= 0;

	/**
	\brief Sets the object reference for a PxSerializable

	This is called by the framework during deserialization, when object references are
	retrieved from a deserialized collection.

	This can also be called by users to link a subset to a completely different
	subset than the one it was originally linked to.

	\param[in]	object		Serializable object. See #PxSerializable
	\param[in]	ref			user-defined reference for this object
	\return		true if success

	@see PxSerializable
	*/

	virtual	bool			setObjectRef(PxSerializable& object, PxSerialObjectRef ref)		= 0;


	/**
	\brief test whether an object is referenced by this UserReferences

	Note that in general the mapping from references to objects may be many-to-one.

	\return whether the object is referenced

	@see setObjectRef getNbObjectRefs

	*/

	virtual bool			objectIsReferenced(PxSerializable& object) const = 0;


	/**
	\brief Retrieves the number of object references

	\return the number of references

	@see getObjectRefs
	*/
	virtual PxU32			getNbObjectRefs() const = 0;


	/**
	\brief Retrieve all object reference pairs

	\param[in]	buffer  an pointer to a buffer into which to write the pairs
	\param[in]	bufSize the size of the buffer
	\return	the number of pairs written

	@see setObjectRef getNbObjectRefs
	*/

	virtual PxU32			getObjectRefs(PxSerialObjectAndRef* buffer, PxU32 bufSize) const = 0;


	/**
	\brief Deletes a user references object.

	@see PxPhysics::createUserReferences()
	*/

	virtual void			release() = 0;
};

struct PxSerialFlag
{
	enum Enum
	{
		eOWNS_MEMORY			= (1<<0),
//		eDISABLED				= (1<<1),
		eDISABLE_AUTO_RESOLVE	= (1<<1),
		eDISABLE_FIELDS			= (1<<2),
		eIN_SCENE				= (1<<3),
	};
};


#if defined(PX_WINDOWS)
//! \cond
template class PX_PHYSX_COMMON_API PxFlags<PxSerialFlag::Enum, PxU16>;  // needed for dll export
//! \endcond
#endif

typedef PxFlags<PxSerialFlag::Enum, PxU16> PxSerialFlags;
PX_FLAGS_OPERATORS(PxSerialFlag::Enum, PxU16);


typedef PxSerializable*	(*PxClassCreationCallback)(char*& address, PxRefResolver& v);


/**
\brief Collection class for serialization.

A collection is a container for serializable SDK objects. All serializable SDK objects inherit from PxSerializable.
Serialization and deserialization only work through collections.

A scene is typically serialized using the following steps:

1) create a collection
2) collect objects to serialize
3) serialize collection
4) release collection

For example the code may look like this:

	PxPhysics* physics;	// The physics SDK object
	PxScene* scene;		// The physics scene
	SerialStream s;		// The user-defined stream doing the actual write to disk

	PxCollection* collection = physics->createCollection();	// step 1)
	PxCollectForExportSDK(*physics, *collection);			// step 2)
	PxCollectForExportScene(*scene, *collection);			// step 2)
	collection->serialize(s);								// step 3)
	physics->releaseCollection(collection);					// step 4)

A scene is typically deserialized using the following steps:

1) load a serialized block somewhere in memory
2) create a collection object
3) deserialize objects (populate collection with objects from the memory block)
4) add collected objects to scene
5) release collection

For example the code may look like this:

	PxPhysics* physics;	// The physics SDK object
	PxScene* scene;		// The physics scene
	void* memory128;	// a 128-byte aligned buffer previously loaded from disk by the user	- step 1)

	PxCollection* collection = physics->createCollection();		// step 2)
	collection->deserialize(memory128, NULL, NULL);				// step 3)
	physics->addCollection(*collection, scene);					// step 4)
	collection->release();										// step 5)

@see PxSerializable
*/
class PxCollection
{
	friend class PxSerializable;
	virtual	void						addUnique(PxSerializable&)	= 0;

public:
										PxCollection()	{}
	virtual								~PxCollection()	{}

	/** \brief deprecated alias for setObjectRef 
	
	\param[in]	object		Serializable object. See #PxSerializable
	\param[in]	ref			user-defined object reference for this object

	@see setObjectRef
	*/
	PX_DEPRECATED PX_INLINE	void		setUserData(PxSerializable& object, PxSerialObjectRef ref)	{	setObjectRef(object, ref); }


	/**
	\brief Serializes a collection.

	Writes out collected objects to a binary stream. Objects are output in the order
	defined by PxSerialOrder, according to their type.

	Object references and external references, as defined by the setObjectRef and
	addExternalRef functions, are also serialized.

	\param[in]	stream		User-defined serialization stream. 
	\param[in]	exportNames	If true, objects' names are serialized along with the objects. Not serializing names produce smaller files.

	@see PxSerialOrder setUserData addExternalRef
	*/

	virtual	void						serialize(PxOutputStream& stream, bool exportNames=false)	= 0;


	/**
	\brief Deserializes a collection.

	Initializes/creates objects within the given input buffer, which must have
	been deserialized from disk already by the user. The input buffer must be
	128-bytes aligned.
	
	Deserialized objects are added to the collection.

	Object references for the collection can be retrieved, if necessary.
	External references can be passed, if necesary.

	\param[in]	buffer128			Deserialized input buffer, 128-bytes aligned
	\param[out]	newReferences		map of new object references created by deserialization, or NULL. See #PxUserReferences
	\param[in]	externalReferences	map for resolving the collection's external references, or NULL. See #PxUserReferences
	\return		True if success

	@see PxUserReferences
	*/
	virtual	bool						deserialize(void* buffer128, PxUserReferences* newReferences, const PxUserReferences* externalReferences)	= 0;

	/**
	\brief Sets user-data/name for a PxSerializable

	This is used to assign an object reference to a PxSerializable. The references are serialized with the collection, and can be used 
	after deserialization to look up objects by reference.

	\param[in]	object		Serializable object. See #PxSerializable
	\param[in]	ref			user-defined object reference for this object
	\return		true if success

	@see PxSerializable addExternalRef getObjectRef
	*/
	virtual	bool						setObjectRef(PxSerializable& object, PxSerialObjectRef ref) = 0;


	/**
	\brief Retrieves the object references specified by the user for this collection. 
	
	note that user references created by deserialization are not returned by this function.

	*/
	virtual PxUserReferences* 			getObjectRefs() const = 0;


	/**
	\brief Declares an reference to an object outside the collection

	Some objects in the collection might have pointers/references to objects that are not within
	the collection. Such objects will not be serialized when the collection is serialized, and the
	references will be marked for resolution when the collection is deserialized.

	\param[in]	object		Serializable object. See #PxSerializable
	\param[in]	ref			user-defined name for this object
	\return		true if success

	@see PxSerializable setObjectRef
	*/
	virtual	bool					addExternalRef(PxSerializable& object, PxSerialObjectRef ref)	= 0;

	/**
	\brief Retrieve all external reference pairs

	\note the pairs retrieved are those set by the application with addExternalRef. No external references are created in the
	collection during deserialization.

	\return	the external references

	@see addExternalRef getNbExternalRefs
	*/

	virtual PxUserReferences*			getExternalRefs() const = 0;


	/**
	\brief Gets number of objects in the collection
	\return	Number of objects in the collection
	*/
	virtual	PxU32						getNbObjects() const = 0;


	/**
	\brief Gets object from the collection

	\param[in]	index	Object index, between 0 (incl) and getNbObjects() (excl).
	\return		Desired object from the collection

	@see PxSerializable
	*/
	virtual	PxSerializable*				getObject(PxU32 index) const = 0;

	/**
	\brief Deletes a collection object.

	This function only deletes the collection object, i.e. the container class. It doesn't delete objects
	that are part of the collection.

	@see PxPhysics::createCollection() 
	*/

	virtual void						release() = 0;
};

class PxNameManager
{
	public:
	virtual ~PxNameManager() {}
	virtual	void						registerName(const char**)	= 0;
};

class PxPtrManager
{
	public:
	virtual ~PxPtrManager() {}
	virtual	void						registerPtr(void*)	= 0;
};

/**
\brief Base class for serializable objects

@see PxRefResolver PxCollection 
*/
class PxSerializable
{
public:

	/**
	\brief returns string name of dynamic type.
	\return	class name of most derived type of this object.
	*/

	virtual		const char*					getConcreteTypeName()		const	{ return NULL;											}

	/* brief dynamic casts a pointer to a pointer to the given type 
	\return a pointer to the given type, or NULL
	*/

	template<class T> T*					is()						{ return typeMatch<T>() ? static_cast<T*>(this) : NULL;			}

	/* brief dynamic casts a pointer-to-const to a pointer to the given type 
	\return a pointer to the given type, or NULL
	*/

	template<class T> const T*				is() const					{ return typeMatch<T>() ? static_cast<const T*>(this) : NULL;		}


	/**
	\brief	Returns concrete type of object.
	\return	PxConcreteType::Enum of serialized object

	@see PxConcreteType
	*/
	PX_INLINE	PxType						getConcreteType()	const	{ return mConcreteType;	}


	virtual		PxU32						getOrder()											const	{ return PxSerialOrder::eDEFAULT;						}

	/**
	\brief Adds an object to the collection.

	\param[in]	c				collection to add the object to

	@see PxCollection
	*/
	virtual		void						collectForExport(PxCollection& c)							{ c.addUnique(*this);									}

	virtual		bool						getFields(PxSerialStream&, PxU32)					const	{ return true;											}
	virtual		bool						getFields(PxSerialStream&, PxField::Enum)			const	{ return true;											}
	virtual		bool						getFields(PxSerialStream&)							const	{ return true;											}
	virtual		const PxFieldDescriptor*	getFieldDescriptor(const char*)						const	{ return NULL;											}

	virtual		PxU32						getObjectSize()										const	= 0;

	virtual		void						exportExtraData(PxSerialStream&)							{														}
	virtual		char*						importExtraData(char* address, PxU32&)						{ return address;										}
	virtual		bool						resolvePointers(PxRefResolver&, void*)						{ return true;											}

	virtual		void						registerNameForExport(PxNameManager&)						{														}
	virtual		void						registerPtrsForExport(PxPtrManager& manager)				{ manager.registerPtr(this);							}

	virtual		void						disableInternalCaching(bool)								{}

	PX_INLINE	void						setSerialFlag(PxSerialFlag::Enum flag, bool value)			{ mSerialFlags = value ? mSerialFlags|flag : mSerialFlags&~flag; }
	PX_INLINE	void						setSerialFlags(PxSerialFlags inFlags )						{ mSerialFlags = inFlags;								}
	PX_INLINE	PxSerialFlags				getSerialFlags()									const	{ return mSerialFlags;									}

	static		void						getMetaData(PxSerialStream& stream);
	virtual									~PxSerializable()											{}

protected:
											PxSerializable(PxRefResolver& v)
											{
												mSerialFlags &= ~PxSerialFlag::eOWNS_MEMORY;
											    PX_UNUSED(v);
											}
											PxSerializable() : mConcreteType(PxConcreteType::eUNDEFINED), mSerialFlags(PxSerialFlag::eOWNS_MEMORY)
											{
											}
	

	PX_INLINE	void						setSerialType(PxType t)											{ mConcreteType = t;														}

	template<class T> bool					typeMatch() const
	{
		return PxU32(PxTypeInfo<T>::eFastTypeId)!=PxU32(PxConcreteType::eUNDEFINED) ? PxU32(getConcreteType()) == PxU32(PxTypeInfo<T>::eFastTypeId)
																					: isKindOf(PxTypeInfo<T>::name());
	}


	virtual		bool						isKindOf(const char* superClass)					const	{ return !strcmp(superClass, "PxSerializable");			}



private:
				PxType						mConcreteType;			// Some kind of class identifier. Could use a string = class name
				PxSerialFlags				mSerialFlags;	// Serialization flags
};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif

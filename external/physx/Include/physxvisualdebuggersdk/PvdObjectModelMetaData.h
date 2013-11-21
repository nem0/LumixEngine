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



#ifndef PVD_OBJECT_MODEL_META_DATA_H
#define PVD_OBJECT_MODEL_META_DATA_H
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxAssert.h"
#include "physxvisualdebuggersdk/PvdObjectModelBaseTypes.h"
#include "physxvisualdebuggersdk/PvdBits.h"


namespace physx { namespace debugger {

	class PvdInputStream;
	class PvdOutputStream;

	struct PropertyDescription
	{
		NamespacedName		mOwnerClassName;
		NonNegativeInteger	mOwnerClassId;
		String				mName;
		String				mSemantic;
		NonNegativeInteger	mDatatype;		//The datatype this property corresponds to.
		NamespacedName		mDatatypeName;	//The name of the datatype
		PropertyType::Enum	mPropertyType;	//Scalar or array.
		NonNegativeInteger	mPropertyId;	//No other property under any class has this id, it is DB-unique.
		PxU32				m32BitOffset;	//Offset in bytes into the object's data section where this property starts.
		PxU32				m64BitOffset;	//Offset in bytes into the object's data section where this property starts.

		PropertyDescription( const NamespacedName& clsName, NonNegativeInteger classId
							, String name, String semantic
							, NonNegativeInteger datatype, const NamespacedName& datatypeName
							, PropertyType::Enum propType, NonNegativeInteger propId, PxU32 offset32, PxU32 offset64 )

				: mOwnerClassName( clsName ), mOwnerClassId( classId )
				, mName( name ), mSemantic( semantic )
				, mDatatype( datatype ), mDatatypeName( datatypeName )
				, mPropertyType( propType )
				, mPropertyId( propId ), m32BitOffset( offset32 ), m64BitOffset( offset64 )
		{
		}
		PropertyDescription()
			: mName( "" )
			, mSemantic( "" )
			, mPropertyType( PropertyType::Unknown )
			, m32BitOffset( 0 )
			, m64BitOffset( 0 )
		{
		}
	};

	struct PtrOffsetType
	{
		enum Enum
		{
			UnknownOffset,
			VoidPtrOffset,
			StringOffset,
		};
	};

	struct PtrOffset
	{
		PtrOffsetType::Enum mOffsetType;
		PxU32				mOffset;
		PtrOffset( PtrOffsetType::Enum type, PxU32 offset )
			: mOffsetType( type ), mOffset( offset ) {}
		PtrOffset() : mOffsetType( PtrOffsetType::UnknownOffset ), mOffset( 0 ) {}
	};
	
	inline PxU32 align( PxU32 offset, PxU32 alignment )
	{
		PxU32 startOffset = offset;
		PxU32 alignmentMask = ~(alignment - 1);
		offset = (offset + alignment - 1) & alignmentMask;
		PX_ASSERT( offset >= startOffset && ( offset % alignment ) == 0 );
		(void)startOffset;
		return offset;
	}

	struct ClassDescriptionSizeInfo
	{
		PxU32					mByteSize;		//The size of the data section of this object, padded to alignment.
		PxU32					mDataByteSize;	//The last data member goes to here.
		PxU32					mAlignment;		//Alignment in bytes of the data section of this object.
		DataRef<PtrOffset>		mPtrOffsets;	//the offsets of string handles in the binary value of this class
		ClassDescriptionSizeInfo() : mByteSize( 0 ), mDataByteSize( 0 ), mAlignment( 0 ) {}
	};

	struct ClassDescription
	{
		NamespacedName					mName;
		NonNegativeInteger				mClassId;		//No other class has this id, it is DB-unique
		NonNegativeInteger				mBaseClass;		//Only single derivation supported.
		//If this class has properties that are of uniform type, then we note that.
		//This means that when deserialization an array of these objects we can just use 
		//single function to endian convert the entire mess at once.
		NonNegativeInteger				mPackedUniformWidth;
		//If this class is composed uniformly of members of a given type
		//Or all of its properties are composed uniformly of members of
		//a give ntype, then this class's packed type is that type.
		//PxTransform's packed type would be PxF32.
		NonNegativeInteger				mPackedClassType;
		ClassDescriptionSizeInfo		m32BitSizeInfo;
		ClassDescriptionSizeInfo		m64BitSizeInfo;
		bool							mLocked;		//No further property additions allowed.
		//True when this datatype has an array on it that needs to be
		//separately deleted.
		bool							mRequiresDestruction;

		ClassDescription(NamespacedName name, NonNegativeInteger id) 
			: mName( name ), mClassId( id ), mLocked( false )
			, mRequiresDestruction( false ) 
		{}
		ClassDescription() : mLocked( false ), mRequiresDestruction( false ) {}

		PxU32 get32BitSize() const { return m32BitSizeInfo.mByteSize; }
		PxU32 get64BitSize() const { return m64BitSizeInfo.mByteSize; }
		const ClassDescriptionSizeInfo& getNativeSizeInfo() const { return sizeof(void*) == 4 ? m32BitSizeInfo : m64BitSizeInfo; }
		PxU32 getNativeSize() const { return sizeof(void*) == 4 ? get32BitSize() : get64BitSize(); }
	};

	//argument to the create property message function
	struct PropertyMessageArg
	{
		String					mPropertyName;
		NamespacedName			mDatatypeName;
		PxU32					mMessageOffset; //where in the message this property starts.
		PxU32					mByteSize;		//size of this entry object

		PropertyMessageArg( String propName, NamespacedName dtype, PxU32 msgOffset, PxU32 byteSize )
			: mPropertyName( propName )
			, mDatatypeName( dtype )
			, mMessageOffset( msgOffset )
			, mByteSize( byteSize )
		{
		}
		PropertyMessageArg() : mPropertyName( "" ), mMessageOffset( 0 ), mByteSize( 0 ) {}
	};
	

	struct MarshalQueryResult
	{
		NonNegativeInteger	srcType;
		NonNegativeInteger	dstType;
		bool				canMarshal;		//If canMarshal != needsMarshalling we have a problem.
		bool				needsMarshalling; 
		TBlockMarshaller	marshaller;		//Non null if marshalling is possible.
		MarshalQueryResult( NonNegativeInteger _srcType = -1, NonNegativeInteger _dstType = -1, bool _canMarshal = false, bool _needs = false, TBlockMarshaller _m  = NULL )
			: srcType( _srcType ), dstType( _dstType ), canMarshal( _canMarshal ), needsMarshalling( _needs ), marshaller( _m ) 
		{}
	};

	struct PropertyMessageEntry
	{
		PropertyDescription		mProperty;
		NamespacedName			mDatatypeName;
		NonNegativeInteger		mDatatypeId;	//datatype of the data in the message.
		PxU32					mMessageOffset; //where in the message this property starts.
		PxU32					mByteSize;		//size of this entry object

		//If the chain of properties doesn't have any array properties this indicates the
		PxU32					mDestByteSize;

		PropertyMessageEntry( PropertyDescription propName, NamespacedName dtypeName, NonNegativeInteger dtype, PxU32 messageOff, PxU32 byteSize, PxU32 destByteSize )
			: mProperty( propName )
			, mDatatypeName( dtypeName )
			, mDatatypeId( dtype )
			, mMessageOffset( messageOff )
			, mByteSize( byteSize )
			, mDestByteSize( destByteSize )
		{
		}
		PropertyMessageEntry() : mMessageOffset( 0 ), mByteSize( 0 ), mDestByteSize( 0 ) {}
	};

	//Create a struct that defines a subset of the properties on an object.
	struct PropertyMessageDescription
	{
		NamespacedName							mClassName;
		NonNegativeInteger						mClassId;		//No other class has this id, it is DB-unique
		NamespacedName							mMessageName;
		NonNegativeInteger						mMessageId;
		DataRef<PropertyMessageEntry>			mProperties;
		PxU32									mMessageByteSize;
		//Offsets into the property message where const char* items are.
		DataRef<PxU32>							mStringOffsets;
		PropertyMessageDescription( const NamespacedName& nm, NonNegativeInteger clsId
									, const NamespacedName& msgName, NonNegativeInteger msgId
									, PxU32 msgSize )
			: mClassName( nm )
			, mClassId( clsId )
			, mMessageName( msgName )
			, mMessageId( msgId )
			, mMessageByteSize( msgSize )
		{
		}
		PropertyMessageDescription() : mMessageByteSize( 0 ) {}
	};

	class StringTable
	{
	protected:
		virtual ~StringTable(){}
	public:
		virtual PxU32 getNbStrs() = 0;
		virtual PxU32 getStrs( const char** outStrs, PxU32 bufLen, PxU32 startIdx = 0 ) = 0;
		virtual const char* registerStr( const char* str, bool& outAdded ) = 0;
		const char* registerStr( const char* str )
		{
			bool ignored;
			return registerStr( str, ignored );
		}
		virtual StringHandle strToHandle( const char* str ) = 0;
		virtual const char* handleToStr( PxU32 hdl ) = 0;
		virtual void release() = 0;

		static StringTable& create( PxAllocatorCallback& alloc );
	};

	/** 
	 *	Create new classes and add properties to some existing ones.
	 *	The default classes are created already, the simple types
	 *  along with the basic math types. 
	 *	(PxU8, PxI8, etc ) 
	 *	(PxVec3, PxQuat, PxTransform, PxMat33, PxMat34, PxMat44)
	 */
	class PvdObjectModelMetaData
	{
	protected:
		virtual ~PvdObjectModelMetaData(){}
	public:
		

		virtual ClassDescription getOrCreateClass( const NamespacedName& nm ) = 0;
		//get or create parent, lock parent. deriveFrom getOrCreatechild.
		virtual bool deriveClass( const NamespacedName& parent, const NamespacedName& child ) = 0;
		virtual Option<ClassDescription> findClass( const NamespacedName& nm ) const = 0;
		template<typename TDataType> Option<ClassDescription> findClass() { return findClass( getPvdNamespacedNameForType<TDataType>() ); }
		virtual Option<ClassDescription> getClass( NonNegativeInteger classId ) const = 0;
		virtual Option<ClassDescription> getParentClass( NonNegativeInteger classId ) const = 0;
		bool isDerivedFrom( NonNegativeInteger classId, NonNegativeInteger parentClass ) const
		{
			if ( classId == parentClass ) return true;
			for ( Option<ClassDescription> p = getParentClass( classId ); p.hasValue(); p = getParentClass( p.getValue().mClassId ) )
				if ( p.getValue().mClassId == parentClass ) return true;
			return false;
		}

		virtual void lockClass( NonNegativeInteger classId ) = 0;

		virtual PxU32 getNbClasses() const = 0;
		virtual PxU32 getClasses( ClassDescription* outClasses, PxU32 requestCount, PxU32 startIndex = 0 ) const = 0;

		//Create a nested property.
		//This way you can have obj.p.x without explicity defining the class p.
		virtual Option<PropertyDescription> createProperty( NonNegativeInteger classId, String name, String semantic, NonNegativeInteger datatype, PropertyType::Enum propertyType = PropertyType::Scalar ) = 0;
		Option<PropertyDescription> createProperty( NamespacedName clsId, String name, String semantic, NamespacedName dtype, PropertyType::Enum propertyType = PropertyType::Scalar )
		{
			return createProperty( findClass( clsId )->mClassId, name, semantic, findClass( dtype )->mClassId, propertyType );
		}
		Option<PropertyDescription> createProperty( NamespacedName clsId, String name, NamespacedName dtype, PropertyType::Enum propertyType = PropertyType::Scalar )
		{
			return createProperty( findClass( clsId )->mClassId, name, "", findClass( dtype )->mClassId, propertyType );
		}
		Option<PropertyDescription> createProperty( NonNegativeInteger clsId, String name, NonNegativeInteger dtype, PropertyType::Enum propertyType = PropertyType::Scalar )
		{
			return createProperty( clsId, name, "", dtype, propertyType );
		}
		template<typename TDataType>
		Option<PropertyDescription> createProperty( NonNegativeInteger clsId, String name, String semantic = "", PropertyType::Enum propertyType = PropertyType::Scalar )
		{
			return createProperty( clsId, name, semantic, getPvdNamespacedNameForType<TDataType>(), propertyType );
		}
		virtual Option<PropertyDescription> findProperty( const NamespacedName& cls, String prop ) const = 0;
		virtual Option<PropertyDescription> findProperty( NonNegativeInteger clsId, String prop ) const = 0;
		virtual Option<PropertyDescription> getProperty( NonNegativeInteger propId ) const = 0;
		virtual void setNamedPropertyValues(DataRef<NamedValue> values, NonNegativeInteger propId ) = 0;
		virtual DataRef<NamedValue> getNamedPropertyValues( NonNegativeInteger propId ) const = 0; //for enumerations and flags.


		virtual PxU32 getNbProperties( NonNegativeInteger classId ) const = 0;
		virtual PxU32 getProperties( NonNegativeInteger classId, PropertyDescription* outBuffer, PxU32 bufCount, PxU32 startIdx = 0 ) const = 0;

		//Check that a property path, starting at the given class id and first property is value.  Return the resolved properties.
		//outbuffer.size *must* equal the propPath.size().
		Option<PropertyDescription> resolvePropertyPath( NonNegativeInteger clsId, const NonNegativeInteger propId ) const
		{
			Option<PropertyDescription> prop( getProperty( propId ) );
			if ( prop.hasValue() == false ) return prop;
			if ( isDerivedFrom( clsId, prop.getValue().mOwnerClassId ) == false ) return None();
			return prop;
		}
		//Does one cls id differ marshalling to another and if so return the functions to do it.
		virtual MarshalQueryResult checkMarshalling( NonNegativeInteger srcClsId, NonNegativeInteger dstClsId ) const = 0;

		//messages and classes are stored in separate maps, so a property message can have the same name as a class.
		virtual Option<PropertyMessageDescription> createPropertyMessage( const NamespacedName& cls, const NamespacedName& msgName, DataRef<PropertyMessageArg> entries, PxU32 messageSize ) = 0;
		virtual Option<PropertyMessageDescription> findPropertyMessage( const NamespacedName& msgName ) const = 0;
		virtual Option<PropertyMessageDescription> getPropertyMessage( NonNegativeInteger msgId ) const = 0;

		virtual PxU32 getNbPropertyMessages() const = 0;
		virtual PxU32 getPropertyMessages( PropertyMessageDescription* msgBuf, PxU32 bufLen, PxU32 startIdx = 0 ) const = 0;

		virtual StringTable& getStringTable() const = 0;

		virtual void write( PvdOutputStream& stream ) const = 0;
		void save( PvdOutputStream& stream ) const { write( stream ); }

		virtual PvdObjectModelMetaData& clone() const = 0;

		virtual void addRef() = 0;
		virtual void release() = 0;

		static PxU32 getCurrentPvdObjectModelVersion();
		static PvdObjectModelMetaData& create( PxAllocatorCallback& allocator );
		static PvdObjectModelMetaData& create( PxAllocatorCallback& allocator, PvdInputStream& stream);
	};
}}

#endif
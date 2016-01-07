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



#ifndef PVD_OBJECT_MODEL_BASE_TYPES_H
#define PVD_OBJECT_MODEL_BASE_TYPES_H
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxAssert.h"

namespace physx { namespace general_shared3 {
	class PxMat34Legacy;
}}

namespace physx { namespace debugger {
	namespace comm {}
	
	using namespace physx;
	using namespace general_shared3;

	inline const char* nonNull( const char* str ) { return str ? str : ""; }
	//strcmp will crash if passed a null string, however,
	//so we need to make sure that doesn't happen.  We do that
	//by equating NULL and the empty string, "".
	inline bool safeStrEq( const char* lhs, const char* rhs )
	{
		return strcmp( nonNull (lhs), nonNull(rhs) ) == 0;
	}

	//Does this string have useful information in it.
	inline bool isMeaningful( const char* str ) { return *(nonNull( str ) ) > 0; }

	inline PxU32 safeStrLen( const char* str )
	{
		str = nonNull( str );
		return static_cast<PxU32>( strlen( str ) );
	}
	struct None
	{
	};

	template<typename T>
	class Option
	{
		T mValue;
		bool mHasValue;

	public:
		Option( const T& val ) : mValue( val ), mHasValue( true ) {}
		Option( None nothing = None() ) : mHasValue( false ) { (void)nothing; }
		Option( const Option& other ) : mValue( other.mValue ), mHasValue( other.mHasValue ) {}
		Option& operator=( const Option& other ) 
		{
			mValue = other.mValue;
			mHasValue = other.mHasValue;
			return *this;
		}
		bool hasValue() const { return mHasValue; }
		const T& getValue() const { PX_ASSERT( hasValue() ); return mValue; }
		T& getValue() { PX_ASSERT( hasValue() ); return mValue; }
		operator const T& () const { return getValue(); }
		operator T& () { return getValue(); }
		T* operator->() { return &getValue(); }
		const T* operator->() const { return &getValue(); }
	};

	class NonNegativeInteger
	{
		PxI32 mValue;
	public:
		NonNegativeInteger( PxI32 val = -1 ) : mValue( val ) {}
		NonNegativeInteger( const NonNegativeInteger& o ) : mValue( o.mValue ) {}
		NonNegativeInteger& operator=( const NonNegativeInteger& o ) { mValue = o.mValue; return *this; }
		bool hasValue() const { return mValue >= 0; }
		PxI32 getValue() const { PX_ASSERT( hasValue() ); return mValue; }
		operator PxI32 () const { return getValue(); }
		PxI32 unsafeGetValue() const { return mValue; }
		bool operator==( const NonNegativeInteger& other ) { return mValue == other.mValue; }
	};

	struct PvdBaseType
	{
		enum Enum
		{
			None = 0,
			InternalStart = 1,
			InternalStop = 64,
#define DECLARE_BASE_PVD_TYPE( type ) type,
	#include "physxvisualdebuggersdk/PvdObjectModelBaseTypeDefs.h"
			Last
#undef DECLARE_BASE_PVD_TYPE
		};
	};

	struct ObjectRef
	{
		NonNegativeInteger				mInstanceId;
		ObjectRef( NonNegativeInteger iid ) : mInstanceId( iid ) {}
		ObjectRef( PxI32 iid = -1 ) : mInstanceId( iid ) {}
		operator NonNegativeInteger() const { return mInstanceId; }
		operator PxI32 () const { return mInstanceId.getValue(); }
		bool hasValue() const { return mInstanceId.unsafeGetValue() > 0; }
	};

	struct U32Array4
	{
		PxU32 mD0;
		PxU32 mD1;
		PxU32 mD2;
		PxU32 mD3;
		U32Array4( PxU32 d0, PxU32 d1, PxU32 d2, PxU32 d3 )	
			: mD0( d0 ), mD1( d1 ), mD2( d2 ), mD3( d3 ) {}
		U32Array4() : mD0( 0 ), mD1( 0 ), mD2( 0 ), mD3( 0 ) {}
	};

#define PVD_POINTER_TO_U64( ptr ) static_cast<PxU64>( reinterpret_cast<size_t>( ptr ) )
#define PVD_U64_TO_POINTER( ptrtype, val ) reinterpret_cast<ptrtype>( static_cast<size_t>( val ) );
	typedef bool PvdBool;
	typedef const char* String;
	typedef void* VoidPtr;

	
	
	struct PvdColor
	{
		PxU8 r;
		PxU8 g;
		PxU8 b;
		PxU8 a;
		PvdColor( PxU8 _r, PxU8 _g, PxU8 _b, PxU8 _a = 255 ) : r( _r ), g( _g ), b( _b ), a( _a ) {}
		PvdColor() : r( 0 ), g( 0 ), b( 0 ), a( 255 ) {}
		PvdColor( PxU32 value )
		{
			PxU8* valPtr = reinterpret_cast<PxU8*>( &value );
			r = valPtr[0];
			g = valPtr[1];
			b = valPtr[2];
			a = valPtr[3];
		}
	};

	struct StringHandle
	{
		PxU32 mHandle;
		StringHandle( PxU32 val = 0 ) : mHandle( val ) {}
		operator PxU32 () const { return mHandle; }
	};
	
	struct NamespacedName
	{
		String mNamespace;
		String mName;
		NamespacedName( String ns, String nm ) : mNamespace( ns ), mName( nm ) {}
		NamespacedName( String nm = "" ) : mNamespace( "" ), mName( nm ) {}
		bool operator==( const NamespacedName& other ) const
		{
			return safeStrEq( mNamespace, other.mNamespace ) 
					&& safeStrEq( mName, other.mName );
		}
	};
	

	struct NamedValue
	{
		String	mName;
		PxU32	mValue;
		NamedValue( String nm = "", PxU32 val = 0 )
			: mName( nm )
			, mValue( val )
		{
		}
	};


	template<typename T>
	struct BaseDataTypeToTypeMap
	{
		bool compile_error;
	};
	template<PvdBaseType::Enum>
	struct BaseTypeToDataTypeMap
	{
		bool compile_error;
	};

	//Users can extend this mapping with new datatypes.
	template<typename T>
	struct PvdDataTypeToNamespacedNameMap
	{
		bool Name;
	};
	//This mapping tells you the what class id to use for the base datatypes

#define DECLARE_BASE_PVD_TYPE( type )																			\
	template<> struct BaseDataTypeToTypeMap<type> { enum Enum { BaseTypeEnum = PvdBaseType::type }; };			\
	template<> struct BaseDataTypeToTypeMap<const type&> { enum Enum { BaseTypeEnum = PvdBaseType::type }; };	\
	template<> struct BaseTypeToDataTypeMap<PvdBaseType::type> { typedef type TDataType; };						\
	template<> struct PvdDataTypeToNamespacedNameMap<type> { NamespacedName Name; PvdDataTypeToNamespacedNameMap<type>() : Name( "physx3", #type ) {} }; \
	template<> struct PvdDataTypeToNamespacedNameMap<const type&> { NamespacedName Name; PvdDataTypeToNamespacedNameMap<const type&>() : Name( "physx3", #type ) {} };

	#include "physxvisualdebuggersdk/PvdObjectModelBaseTypeDefs.h"
#undef DECLARE_BASE_PVD_TYPE

	template< typename TDataType> inline NonNegativeInteger getPvdTypeForType() { return static_cast<PvdBaseType::Enum>(BaseDataTypeToTypeMap<TDataType>::BaseTypeEnum); }
	template<typename TDataType> inline NamespacedName getPvdNamespacedNameForType() { return PvdDataTypeToNamespacedNameMap<TDataType>().Name; }

#define DEFINE_PVD_TYPE_NAME_MAP( type, ns, name ) \
	template<> struct PvdDataTypeToNamespacedNameMap<type> { NamespacedName Name; PvdDataTypeToNamespacedNameMap<type>() : Name( ns, name ) {} };

#define DEFINE_PVD_TYPE_ALIAS( newType, oldType ) \
	template<> struct PvdDataTypeToNamespacedNameMap<newType> { NamespacedName Name; PvdDataTypeToNamespacedNameMap<newType>() : Name( PvdDataTypeToNamespacedNameMap<oldType>().Name ) {} };

DEFINE_PVD_TYPE_ALIAS( const void*, void* )


	struct ArrayData
	{
		PxU8* mBegin;
		PxU8* mEnd;
		PxU8* mCapacity; //>= stop
		ArrayData( PxU8* beg = NULL, PxU8* end = NULL, PxU8* cap = NULL )
			: mBegin(beg )
			, mEnd( end )
			, mCapacity( cap )
		{}
		PxU8* begin() { return mBegin; }
		PxU8* end() { return mEnd; }
		PxU32 byteCapacity() { return static_cast<PxU32>( mCapacity - mBegin ); }
		PxU32 byteSize() const { return static_cast<PxU32>( mEnd - mBegin ); } //in bytes
		PxU32 numberOfItems( PxU32 objectByteSize ) { if ( objectByteSize ) return byteSize() / objectByteSize; return 0; }

		void forgetData() { mBegin = mEnd = mCapacity = 0; }
	};

	template<typename TItemType>
	struct PvdScopedItem
	{
	private:
		PvdScopedItem( const PvdScopedItem& );
		PvdScopedItem& operator=( const PvdScopedItem& );
	public:

		TItemType* mItem;
		PvdScopedItem( TItemType& item ) : mItem( &item ) {}
		PvdScopedItem( TItemType* item ) : mItem( item ) {}
		~PvdScopedItem() { if ( mItem ) mItem->release(); }
		TItemType* operator->() { PX_ASSERT( mItem ); return mItem; }
		TItemType& operator*() { PX_ASSERT( mItem ); return *mItem; }
		operator TItemType* () { return mItem; }
		operator const TItemType* () const { return mItem; }
	};
	

	template<typename T>
	class DataRef
	{
		const T* mBegin;
		const T* mEnd;
	public:
		DataRef( const T* b, PxU32 count ) : mBegin( b ), mEnd( b + count ) {}
		DataRef( const T* b = NULL, const T* e = NULL ) : mBegin( b ), mEnd( e ) {}
		DataRef( const DataRef& o ) : mBegin( o.mBegin ), mEnd( o.mEnd ) {}
		DataRef& operator=( const DataRef& o ) { mBegin = o.mBegin; mEnd = o.mEnd; return *this; } 
		PxU32 size() const { return static_cast<PxU32>( mEnd - mBegin ); }
		const T* begin() const { return mBegin; }
		const T* end() const { return mEnd; }
		const T& operator[]( PxU32 idx ) const { PX_ASSERT( idx < size() ); return mBegin[idx]; }
		const T& back() const { PX_ASSERT( mEnd > mBegin ); return *(mEnd - 1); }
	};
	
	inline PxU64 toPaddedSize( PxU64 inOriginal, PxU32 inPageSize = 0x1000 )
	{
		return (inOriginal + inPageSize) - inOriginal % inPageSize;
	}

	template<PxU32 TSize>
	struct Union
	{
		PxU8 mData[TSize];
		Union(){}
		template<typename TDataType>
		void set( const TDataType& inValue ) { PX_COMPILE_TIME_ASSERT( sizeof( TDataType ) <= TSize ); new (mData) TDataType( inValue ); }
		template<typename TDataType>
		TDataType get() const { PX_COMPILE_TIME_ASSERT( sizeof( TDataType ) <= TSize ); return *reinterpret_cast<const TDataType*>( mData ); }
	};

	struct PropertyType
	{
		enum Enum
		{
			Unknown = 0,
			Scalar,
			Array
		};
	};

	template<typename TObjType>
	struct PvdRefPtr
	{
		mutable TObjType* mObj;
		~PvdRefPtr()
		{
			release();
		}
		PvdRefPtr( TObjType* obj = NULL ) : mObj( obj ) 
		{
			addRef();
		}
		PvdRefPtr( const PvdRefPtr<TObjType>& other )
		{
			mObj = other.mObj;
			addRef();
		}
		PvdRefPtr<TObjType>& operator=( const PvdRefPtr<TObjType>& other )
		{
			if ( mObj != other.mObj )
				release();
			mObj = other.mObj;
			addRef();
			return *this;
		}
		void addRef() { if ( mObj ) mObj->addRef(); }
		void release() { if ( mObj ) mObj->release(); }
		operator TObjType* () { return mObj; }
		operator const TObjType* () const { return mObj; }
		
		TObjType* operator->() { PX_ASSERT( mObj ); return mObj; }
		const TObjType* operator->() const { PX_ASSERT( mObj ); return mObj; }
		
		TObjType& operator*() { PX_ASSERT( mObj ); return *mObj; }
		const TObjType& operator*() const { PX_ASSERT( mObj ); return *mObj; }
	};
}}


#endif

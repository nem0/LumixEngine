/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_DATA_STREAM_H
#define PVD_DATA_STREAM_H
#include "PvdErrorCodes.h"
#include "PvdObjectModelBaseTypes.h"

namespace physx { namespace debugger {
	class PvdObjectModelMetaData;
	struct PropertyMessageArg;
	struct NamespacedName;
}}

namespace physx { namespace debugger { namespace comm {

	class PvdConnection;
	class PvdCommStreamEventSink;
	class PvdOMMetaDataProvider;
	class PvdPropertyDefinitionHelper;

	class PvdMetaDataStream
	{
	protected:
		virtual ~PvdMetaDataStream(){}
	public:

		virtual PvdError createClass( const NamespacedName& nm ) = 0;
		template<typename TDataType>
		PvdError createClass()
		{
			return createClass( getPvdNamespacedNameForType<TDataType>() );
		}
		virtual PvdError deriveClass( const NamespacedName& parent, const NamespacedName& child ) = 0;
		template<typename TParentType, typename TChildType>
		PvdError deriveClass()
		{
			return deriveClass( getPvdNamespacedNameForType<TParentType>(), getPvdNamespacedNameForType<TChildType>() );
		}
		virtual PvdError createProperty( const NamespacedName& clsName, String name, String semantic
										, const NamespacedName& dtypeName, PropertyType::Enum propertyType
										, DataRef<NamedValue> values = DataRef<NamedValue>() ) = 0;
		template<typename TClsType, typename TDataType>
		PvdError createProperty( String name, String semantic = "", PropertyType::Enum propertyType = PropertyType::Scalar, DataRef<NamedValue> values = DataRef<NamedValue>() )
		{
			return createProperty( getPvdNamespacedNameForType<TClsType>(), name, semantic, 
									getPvdNamespacedNameForType<TDataType>(), propertyType, values );
		}

		virtual PvdError createPropertyMessage( const NamespacedName& cls, const NamespacedName& msgName
													, DataRef<PropertyMessageArg> entries, PxU32 messageSizeInBytes ) = 0;

		template<typename TClsType, typename TMsgType>
		PvdError createPropertyMessage( DataRef<PropertyMessageArg> entries )
		{
			return createPropertyMessage( getPvdNamespacedNameForType<TClsType>(), getPvdNamespacedNameForType<TMsgType>(), entries, sizeof( TMsgType ) );
		}
	};

	class PvdInstanceDataStream
	{
	protected:
		virtual ~PvdInstanceDataStream(){}
	public:

		virtual PvdError createInstance( const NamespacedName& cls, const void* instance ) = 0;

		template<typename TDataType>
		PvdError createInstance( const TDataType* inst )
		{
			return createInstance( getPvdNamespacedNameForType<TDataType>(), inst );
		}
		virtual bool isInstanceValid( const void* instance ) = 0;

		//If the property will fit or is already completely in memory
		virtual PvdError setPropertyValue( const void* instance, String name, DataRef<const PxU8> data, const NamespacedName& incomingTypeName ) = 0;
		template<typename TDataType>
		PvdError setPropertyValue( const void* instance, String name, const TDataType& value ) 
		{
			const PxU8* dataStart = reinterpret_cast<const PxU8*>( &value );
			return setPropertyValue( instance, name, DataRef<const PxU8>( dataStart, dataStart + sizeof( TDataType ) ), getPvdNamespacedNameForType<TDataType>() );
		}
		
		template<typename TDataType>
		PvdError setPropertyValue( const void* instance, String name, const TDataType* value, PxU32 numItems )
		{
			const PxU8* dataStart = reinterpret_cast<const PxU8*>( value );
			return setPropertyValue( instance, name, DataRef<const PxU8>( dataStart, dataStart + sizeof( TDataType ) * numItems ), getPvdNamespacedNameForType<TDataType>() );
		}

		//Else if the property is very large (contact reports) you can send it in chunks.
		virtual PvdError beginSetPropertyValue( const void* instance, String name, const NamespacedName& incomingTypeName ) = 0;
		
		template<typename TDataType>
		PvdError beginSetPropertyValue( const void* instance, String name )
		{
			return beginSetPropertyValue( instance, name, getPvdNamespacedNameForType<TDataType>() );
		}
		virtual PvdError appendPropertyValueData( DataRef<const PxU8> data ) = 0;
		
		template<typename TDataType>
		PvdError appendPropertyValueData( const TDataType* value, PxU32 numItems )
		{
			const PxU8* dataStart = reinterpret_cast<const PxU8*>( value );
			return appendPropertyValueData( DataRef<const PxU8>( dataStart, dataStart + numItems * sizeof( TDataType ) ) );
		}

		virtual PvdError endSetPropertyValue() = 0;

		//Set a set of properties to various values on an object.
		
		virtual PvdError setPropertyMessage( const void* instance, const NamespacedName& msgName, DataRef<const PxU8> data ) = 0;
		
		template<typename TDataType>
		PvdError setPropertyMessage( const void* instance, const TDataType& value )
		{
			const PxU8* dataStart = reinterpret_cast<const PxU8*>( &value );
			return setPropertyMessage( instance, getPvdNamespacedNameForType<TDataType>(), DataRef<const PxU8>( dataStart, sizeof( TDataType ) ) );
		}
		//If you need to send of lot of identical messages, this avoids a hashtable lookup per message.
		virtual PvdError beginPropertyMessageGroup( const NamespacedName& msgName ) = 0;
		
		template<typename TDataType>
		PvdError beginPropertyMessageGroup()
		{
			return beginPropertyMessageGroup( getPvdNamespacedNameForType<TDataType>() );
		}
		virtual PvdError sendPropertyMessageFromGroup( const void* instance, DataRef<const PxU8> data ) = 0;
		
		template<typename TDataType>
		PvdError sendPropertyMessageFromGroup( const void* instance, const TDataType& value )
		{
			const PxU8* dataStart = reinterpret_cast<const PxU8*>( &value );
			return sendPropertyMessageFromGroup( instance, DataRef<const PxU8>( dataStart, sizeof( TDataType ) ) );
		}

		virtual PvdError endPropertyMessageGroup() = 0;
		
		//These functions ensure the target array doesn't contain duplicates
		virtual PvdError pushBackObjectRef( const void* instId, String propName, const void* objRef ) = 0;
		virtual PvdError removeObjectRef( const void* instId, String propName, const void* objRef ) = 0;

		//Instance elimination.
		virtual PvdError destroyInstance( const void* key ) = 0;

		
		//Profiling hooks
		virtual PvdError beginSection( const void* instance, String name ) = 0;
		virtual PvdError endSection( const void* instance, String name ) = 0;

		//Origin Shift
		virtual PvdError originShift( const void* scene, PxVec3 shift ) = 0;

	public:
		/*For some cases, pvd command cannot be run immediately. For example, when create joints, while the actors may still pending for insert,
		*the joints update commands can be run deffered.
		*/
		class PvdCommand
		{
		public:
			//Assigned is needed for copying
			PvdCommand(const PvdCommand &){}
			PvdCommand &operator=(const PvdCommand &){return *this;}
		public:
			PvdCommand(){}
			virtual ~PvdCommand(){}

			//Not pure virtual so can have default PvdCommand obj
			virtual bool canRun(PvdInstanceDataStream & ){return false;}
			virtual void run(PvdInstanceDataStream&){}
		};

		//PVD SDK provide this helper function to allocate cmd's memory and release them at after flush the command queue
		virtual void* allocateMemForCmd( PxU32 length ) = 0;

		//PVD will call the destructor of PvdCommand object at the end fo flushPvdCommand
		virtual void pushPvdCommand( PvdCommand& cmd ) = 0;
		virtual void flushPvdCommand() = 0;
	};

	class PvdDataStream : public PvdInstanceDataStream, public PvdMetaDataStream
	{
	protected:
		virtual ~PvdDataStream(){}

	public:
		virtual void addRef() = 0;
		virtual void release() = 0;
		virtual bool isConnected() = 0;
		
		virtual PvdPropertyDefinitionHelper& getPropertyDefinitionHelper() = 0;
		//flushes the data to the connections socket layer which may have further caching.
		//This stream is meant to be used on a per-thread basis, and thus buffers its messages
		//before grabbing the socket mutex and sending them.
		virtual PvdError flush() = 0;
	};
}}}

#endif

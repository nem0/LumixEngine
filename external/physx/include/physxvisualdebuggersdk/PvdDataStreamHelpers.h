/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_DATA_STREAM_HELPERS_H
#define PVD_DATA_STREAM_HELPERS_H
#include "PvdObjectModelBaseTypes.h"
namespace physx { namespace debugger { namespace comm {
	class PvdPropertyDefinitionHelper
	{
	protected:
		virtual ~PvdPropertyDefinitionHelper(){}
	public:
		/**
			Push a name c such that it appends such as a.b.c.
		*/
		virtual void pushName( const char* inName, const char* inAppendStr = "." ) = 0;
		/**
			Push a name c such that it appends like a.b[c]
		*/
		virtual void pushBracketedName( const char* inName, const char* leftBracket = "[", const char* rightBracket = "]" ) = 0;
		/**
		 *	Pop the current name
		 */
		virtual void popName() = 0;

		virtual void clearNameStack() = 0;
		/**
		 *	Get the current name at the top of the name stack.  
		 *	Would return "a.b.c" or "a.b[c]" in the above examples.
		 */
		virtual const char* getTopName() = 0;

		virtual void addNamedValue( const char* name, PxU32 value ) = 0;
		virtual void clearNamedValues() = 0;
		virtual DataRef<NamedValue> getNamedValues() = 0;

		/**
		 *	Define a property using the top of the name stack and the passed-in semantic
		 */
		virtual void createProperty( const NamespacedName& clsName, const char* inSemantic, const NamespacedName& dtypeName, PropertyType::Enum propType = PropertyType::Scalar ) = 0;

		template<typename TClsType, typename TDataType>
		void createProperty( const char* inSemantic = "", PropertyType::Enum propType = PropertyType::Scalar )
		{
			createProperty( getPvdNamespacedNameForType<TClsType>(), inSemantic, getPvdNamespacedNameForType<TDataType>(), propType );
		}

		//The datatype used for instances needs to be pointer unless you actually have Pvd::InstanceId members on your value structs.
		virtual void addPropertyMessageArg( const NamespacedName& inDatatype, PxU32 inOffset, PxU32 inSize ) = 0;

		template<typename TDataType>
		void addPropertyMessageArg( PxU32 offset )
		{
			addPropertyMessageArg( getPvdNamespacedNameForType<TDataType>(), offset, static_cast<PxU32>( sizeof( TDataType ) ) );
		}
		virtual void addPropertyMessage( const NamespacedName& clsName, const NamespacedName& msgName, PxU32 inStructSizeInBytes ) = 0;
		template<typename TClsType, typename TMsgType>
		void addPropertyMessage()
		{
			addPropertyMessage( getPvdNamespacedNameForType<TClsType>(), getPvdNamespacedNameForType<TMsgType>(), static_cast<PxU32>( sizeof( TMsgType ) ) );
		}
		virtual void clearPropertyMessageArgs() = 0;

		void clearBufferedData() { clearNameStack(); clearPropertyMessageArgs(); clearNamedValues(); }
	};
}}}
#endif

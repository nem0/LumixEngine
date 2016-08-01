/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_MEMORY_EVENT_TYPES_H
#define PX_PROFILE_MEMORY_EVENT_TYPES_H

#include "physxprofilesdk/PxProfileBase.h"
#include "foundation/PxBroadcastingAllocator.h"
#include "physxprofilesdk/PxProfileEventBufferClientManager.h"
#include "physxprofilesdk/PxProfileEventSender.h"

namespace physx
{
	//Record events so a late-connecting client knows about
	//all outstanding allocations
	class PxProfileMemoryEventRecorder : public PxAllocationListener
	{
	protected:
		virtual ~PxProfileMemoryEventRecorder(){}
	public:
		virtual void setListener( PxAllocationListener* inListener ) = 0;
		virtual void release() = 0;

		static PxProfileMemoryEventRecorder& createRecorder( PxFoundation* inFoundation );
	};

	class PxProfileMemoryEventBuffer 
		: public PxAllocationListener //add a new event to the buffer
		, public PxProfileEventBufferClientManager //add clients to handle the serialized memory events
		, public PxProfileEventFlusher //flush the buffer
	{
	protected:
		virtual ~PxProfileMemoryEventBuffer(){}
	public:

		virtual void release() = 0;

		//Create a non-mutex-protected event buffer.
		static PxProfileMemoryEventBuffer& createMemoryEventBuffer( PxFoundation* inFoundation, PxU32 inBufferSize = 0x1000 );
		static PxProfileMemoryEventBuffer& createMemoryEventBuffer( PxAllocatorCallback& inAllocator, PxU32 inBufferSize = 0x1000 );
	};

	struct PxProfileMemoryEventType
	{
		enum Enum
		{
			Unknown = 0,
			Allocation,
			Deallocation
		};
	};

	struct PxProfileBulkMemoryEvent
	{
		PxU64 mAddress;
		PxU32 mDatatype;
		PxU32 mFile;
		PxU32 mLine;
		PxU32 mSize;
		PxProfileMemoryEventType::Enum mType;

		PxProfileBulkMemoryEvent(){}

		PxProfileBulkMemoryEvent( PxU32 size, PxU32 type, PxU32 file, PxU32 line, PxU64 addr )
			: mAddress( addr )
			, mDatatype( type )
			, mFile( file )
			, mLine( line )
			, mSize( size )
			, mType( PxProfileMemoryEventType::Allocation )
		{
		}
		
		PxProfileBulkMemoryEvent( PxU64 addr )
			: mAddress( addr )
			, mDatatype( 0 )
			, mFile( 0 )
			, mLine( 0 )
			, mSize( 0 )
			, mType( PxProfileMemoryEventType::Deallocation )
		{
		}
	};
	
	class PxProfileBulkMemoryEventHandler
	{
	protected:
		virtual ~PxProfileBulkMemoryEventHandler(){}
	public:
		virtual void handleEvents( const PxProfileBulkMemoryEvent* inEvents, PxU32 inBufferSize ) = 0;
		static void parseEventBuffer( const PxU8* inBuffer, PxU32 inBufferSize, PxProfileBulkMemoryEventHandler& inHandler, bool inSwapBytes, PxAllocatorCallback* inAlloc = NULL );
	};
}

#endif // PX_PROFILE_MEMORY_EVENT_TYPES_H

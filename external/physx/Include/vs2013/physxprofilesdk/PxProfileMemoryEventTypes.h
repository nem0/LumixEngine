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

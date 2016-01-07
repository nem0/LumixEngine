/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_HANDLER_H
#define PX_PROFILE_EVENT_HANDLER_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"
#include "physxprofilesdk/PxProfileEvents.h"

namespace physx {

	//A client of the event system can expect to find these events in the event buffer.
	class PxProfileEventHandler
	{
	protected:
		virtual ~PxProfileEventHandler(){}
	public:
		virtual void onStartEvent( const PxProfileEventId& inId, PxU32 threadId, PxU64 contextId, PxU8 cpuId, PxU8 threadPriority, PxU64 timestamp ) = 0;
		virtual void onStopEvent( const PxProfileEventId& inId, PxU32 threadId, PxU64 contextId, PxU8 cpuId, PxU8 threadPriority, PxU64 timestamp ) = 0;
		virtual void onEventValue( const PxProfileEventId& inId, PxU32 threadId, PxU64 contextId, PxI64 inValue ) = 0;
		virtual void onCUDAProfileBuffer( PxU64 submitTimestamp, PxF32 timeSpanInMilliseconds, const PxU8* cudaData, PxU32 bufLenInBytes, PxU32 bufferVersion ) = 0;
		static void parseEventBuffer( const PxU8* inBuffer, PxU32 inBufferSize, PxProfileEventHandler& inHandler, bool inSwapBytes );

		/**
			\brief Translates event duration in timestamp (cycles) into nanoseconds.
			
			\param[in] duration Timestamp duration of the event.

			\return event duration in nanoseconds. 
		 */
		static PxU64 durationToNanoseconds(PxU64 duration);
	};

	class PxProfileBulkEventHandler
	{
	protected:
		virtual ~PxProfileBulkEventHandler(){}
	public:
		virtual void handleEvents( const physx::profile::Event* inEvents, PxU32 inBufferSize ) = 0;
		static void parseEventBuffer( const PxU8* inBuffer, PxU32 inBufferSize, PxProfileBulkEventHandler& inHandler, bool inSwapBytes );
	};
}

#endif // PX_PROFILE_EVENT_HANDLER_H

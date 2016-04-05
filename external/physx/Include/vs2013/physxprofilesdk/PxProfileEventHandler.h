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

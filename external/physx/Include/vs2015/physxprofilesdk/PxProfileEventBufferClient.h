/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_BUFFER_CLIENT_H
#define PX_PROFILE_EVENT_BUFFER_CLIENT_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventNames.h"

namespace physx {
	
	/**
	 *	Client handles the data when an event buffer flushes.  This data
	 *	can be parsed (PxProfileEventHandler.h) as a binary set of events.
	 */
	class PxProfileEventBufferClient
	{
	protected:
		virtual ~PxProfileEventBufferClient(){}
	public:
		/**
		 *	Callback when the event buffer is full.  This data is serialized profile events
		 *	and can be read back using:
		 *	PxProfileEventHandler::parseEventBuffer (PxProfileEventHandler.h).
		 */
		virtual void handleBufferFlush( const PxU8* inData, PxU32 inLength ) = 0;
		//Happens if something removes all the clients from the manager.
		virtual void handleClientRemoved() = 0; 
	};

	class PxProfileZoneClient : public PxProfileEventBufferClient
	{
	protected:
		virtual ~PxProfileZoneClient(){}
	public:
		virtual void handleEventAdded( const PxProfileEventName& inName ) = 0;
	};

}


#endif // PX_PROFILE_EVENT_BUFFER_CLIENT_H

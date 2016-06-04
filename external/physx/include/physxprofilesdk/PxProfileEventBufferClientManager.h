/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_BUFFER_CLIENT_MANAGER_H
#define PX_PROFILE_EVENT_BUFFER_CLIENT_MANAGER_H

#include "physxprofilesdk/PxProfileEventBufferClient.h"
namespace physx {
	
	/**
	 *	Managers keep collections of clients.  
	 */
	class PxProfileEventBufferClientManager
	{
	protected:
		virtual ~PxProfileEventBufferClientManager(){}
	public:
		virtual void addClient( PxProfileEventBufferClient& inClient ) = 0;
		virtual void removeClient( PxProfileEventBufferClient& inClient ) = 0;
		virtual bool hasClients() const = 0;
	};

	class PxProfileZoneClientManager
	{
	protected:
		virtual ~PxProfileZoneClientManager(){}
	public:
		virtual void addClient( PxProfileZoneClient& inClient ) = 0;
		virtual void removeClient( PxProfileZoneClient& inClient ) = 0;
		virtual bool hasClients() const = 0;
	};
}

#endif // PX_PROFILE_EVENT_BUFFER_CLIENT_MANAGER_H

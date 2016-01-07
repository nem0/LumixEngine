/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_SYSTEM_H
#define PX_PROFILE_EVENT_SYSTEM_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventSender.h"
#include "physxprofilesdk/PxProfileEventBufferClient.h"
#include "physxprofilesdk/PxProfileEventBufferClientManager.h"

namespace physx {
	class PxProfileContextProvider;
	class PxProfileEventMutex;
	class PxProfileEventFilter;

	/**
	 *	Wraps the different interfaces into one object.
	 */
	class PxProfileEventSystem : public PxProfileEventSender
							, public PxProfileEventBufferClient
							, public PxProfileEventBufferClientManager
							, public PxProfileEventFlusher
	{
	protected:
		~PxProfileEventSystem(){}
	public:
		virtual void release() = 0;
	};
}

#endif // PX_PROFILE_EVENT_SYSTEM_H

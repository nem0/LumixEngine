/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_NAMES_H
#define PX_PROFILE_EVENT_NAMES_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"

namespace physx {

	//Mapping from event id to name.
	struct PxProfileEventName
	{
		const char*					mName;
		PxProfileEventId			mEventId;
		PxProfileEventName( const char* inName, PxProfileEventId inId ) : mName( inName ), mEventId( inId ) {}
	};

	//Aggregator of event id -> name mappings
	struct PxProfileNames
	{
		PxU32							mEventCount;
		const PxProfileEventName*		mEvents;
		//Default constructor that doesn't point to any names.
		PxProfileNames( PxU32 inEventCount = 0, const PxProfileEventName* inSubsystems = NULL )
			: mEventCount( inEventCount )
			, mEvents( inSubsystems )
		{
		}
	};

	/**
		Provides a mapping from event ID -> name.
	*/
	class PxProfileNameProvider
	{
	public:
		virtual PxProfileNames getProfileNames() const = 0;
	protected:
		virtual ~PxProfileNameProvider(){}
		PxProfileNameProvider& operator=(const PxProfileNameProvider&) { return *this; }
	};
}

#endif // PX_PROFILE_EVENT_NAMES_H

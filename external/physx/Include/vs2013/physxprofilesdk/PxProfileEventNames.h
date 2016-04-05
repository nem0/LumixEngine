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
	protected:
		virtual ~PxProfileNameProvider(){}
	public:
		virtual PxProfileNames getProfileNames() const = 0;
	};
}

#endif // PX_PROFILE_EVENT_NAMES_H

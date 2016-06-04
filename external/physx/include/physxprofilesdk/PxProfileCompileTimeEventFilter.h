/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_COMPILE_TIME_EVENT_FILTER_H
#define PX_PROFILE_COMPILE_TIME_EVENT_FILTER_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"

//Define before including header in order to enable a different
//compile time event profile threshold.
#ifndef PX_PROFILE_EVENT_PROFILE_THRESHOLD
#define PX_PROFILE_EVENT_PROFILE_THRESHOLD EventPriorities::Medium
#endif

namespace physx { namespace profile {

	struct EventPriorities
	{
		enum Enum
		{
			None,		// the filter setting to kill all events
			Coarse,
			Medium,
			Detail,
			Never		// the priority to set for an event if it should never fire.
		};
	};

	//Gets the priority for a given event.
	//specialize this object in order to get the priorities setup correctly
	template<PxU16 TEventId>
	struct EventPriority { static const PxU32 val = EventPriorities::Medium; };


	template<PxU16 TEventId>
	struct EventFilter
	{
		static const bool val = EventPriority<TEventId>::val <= PX_PROFILE_EVENT_PROFILE_THRESHOLD;
	};

}}

#define PX_PROFILE_EVENT_PRIORITY_VALUE(subsystem, eventId ) physx::profile::EventPriority<physx::profile::EventIds::subsystem##eventId>::val
#define PX_PROFILE_EVENT_FILTER_VALUE( subsystem, eventId ) physx::profile::EventFilter<physx::profile::EventIds::subsystem##eventId>::val
#define PX_PROFILE_EVENT_ID( subsystem, eventId ) physx::PxProfileCompileTimeFilteredEventId<PX_PROFILE_EVENT_FILTER_VALUE( subsystem, eventId )>( physx::profile::EventIds::subsystem##eventId )

#endif // PX_PROFILE_COMPILE_TIME_EVENT_FILTER_H

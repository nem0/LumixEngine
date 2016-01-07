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

#ifndef PX_PROFILE_ZONE_H
#define PX_PROFILE_ZONE_H

#include "physxprofilesdk/PxProfileEventBufferClientManager.h"
#include "physxprofilesdk/PxProfileEventNames.h"
#include "physxprofilesdk/PxProfileEventFilter.h"
#include "physxprofilesdk/PxProfileEventSender.h"

namespace physx {

	class PxUserCustomProfiler;

	class PxProfileZoneManager;
	/**
	 *	The profiling system was setup in the expectation that there would be several
	 *	systems that each had its own island of profile information.  PhysX, client code,
	 *	and APEX would be the first examples of these.  Each one of these islands is represented
	 *	by a profile zone.
	 *
	 *	A profile zone combines a name, a place where all the events coming from its interface
	 *	can flushed, and a mapping from event number to full event name.
	 *	
	 *	It also provides a top level filtering service where profile events
	 *	can be filtered by event id.  
	 *
	 *	The profile zone implements a system where if there is no one
	 *	listening to events it doesn't provide a mechanism to send them.  In this way
	 *	the event system is short circuited when there aren't any clients.
	 *
	 *	All functions on this interface should be considered threadsafe.
	 */
	class PxProfileZone : public PxProfileZoneClientManager
						, public PxProfileNameProvider
						, public PxProfileEventSender
						, public PxProfileEventFlusher
	{
	protected:
		virtual ~PxProfileZone(){}
	public:
		virtual const char* getName() = 0;
		virtual void release() = 0;

		virtual void setProfileZoneManager(PxProfileZoneManager* inMgr) = 0;
		virtual PxProfileZoneManager* getProfileZoneManager() = 0;

		//Get or create a new event id for a given name.
		//If you pass in a previously defined event name (including one returned)
		//from the name provider) you will just get the same event id back.
		virtual PxU16 getEventIdForName( const char* inName ) = 0;

		/**
			\brief Reserve a contiguous set of profile event ids for a set of names.
			
			This function does not do any meaningful error checking other than to ensure
			that if it does generate new ids they are contiguous.  If the first name is already
			registered, that is the ID that will be returned regardless of what other
			names are registered.  Thus either use this function alone (without the above
			function) or don't use it.  
			If you register "one","two","three" and the function returns an id of 4, then
			"one" is mapped to 4, "two" is mapped to 5, and "three" is mapped to 6.

			\param inNames set of names to register.
			\param inLen Length of the name list.

			\return The first id associated with the first name.  The rest of the names
			will be associated with monotonically incrementing PxU16 values from the first
			id.  
		 */
		virtual PxU16 getEventIdsForNames( const char** inNames, PxU32 inLen ) = 0;

		/**
			\brief Specifies an optional user custom profiler interface for this profile zone.
			\param up Specifies the PxUserCustomProfiler interface for this zone.  A NULL disables event notification.
		 */
		virtual void setUserCustomProfiler(PxUserCustomProfiler* up) = 0;
		/**
			\brief Create a new profile zone.  

			\param inFoundation memory allocation is controlled through the foundation if one is passed in.
			\param inSDKName Name of the profile zone; useful for clients to understand where events came from.
			\param inProvider Mapping from event id -> event name.
			\param inEventBufferByteSize Size of the canonical event buffer.  This does not need to be a large number
				as profile events are fairly small individually.
			\return a profile zone implementation.
		 */
		static PxProfileZone& createProfileZone( PxFoundation* inFoundation, const char* inSDKName, PxProfileNames inNames = PxProfileNames(), PxU32 inEventBufferByteSize = 0x4000 /*16k*/ );
		static PxProfileZone& createProfileZone( PxAllocatorCallback* inAllocator, const char* inSDKName, PxProfileNames inNames = PxProfileNames(), PxU32 inEventBufferByteSize = 0x4000 /*16k*/ );

		
		/** deprecated forms of the above functions */
		static PxProfileZone& createProfileZone( PxFoundation* inFoundation, const char* inSDKName, PxProfileNameProvider& inProvider, PxU32 inEventBufferByteSize = 0x4000 /*16k*/ );
		static PxProfileZone& createProfileZone( PxAllocatorCallback* inAllocator, const char* inSDKName, PxProfileNameProvider& inProvider, PxU32 inEventBufferByteSize = 0x4000 /*16k*/ );
	};
}
#endif // PX_PROFILE_ZONE_H

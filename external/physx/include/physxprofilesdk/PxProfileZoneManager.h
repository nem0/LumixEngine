/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_ZONE_MANAGER_H
#define PX_PROFILE_ZONE_MANAGER_H

#include "physxprofilesdk/PxProfileEventSender.h"
#include "physxprofilesdk/PxProfileEventNames.h"

namespace physx {
	class PxProfileZone;
	class PxProfileNameProvider;

	class PxProfileZoneHandler
	{
	protected:
		virtual ~PxProfileZoneHandler(){}
	public:
		/*
		 *	Not a threadsafe call; handlers are expected to be able to handle
		 *	this from any thread.
		 */
		virtual void onZoneAdded( PxProfileZone& inSDK ) = 0;
		virtual void onZoneRemoved( PxProfileZone& inSDK ) = 0;
	};

	class PxUserCustomProfiler
	{
	public:
		virtual void onStartEvent( const char* eventName, PxU64 contextId, PxU32 threadId) = 0;
		virtual void onStopEvent( const char* eventName, PxU64 contextId, PxU32 threadId) = 0;
		virtual void onEventValue(const char* eventValue, PxI64 inValue) = 0;

	protected:
		virtual ~PxUserCustomProfiler(void) {}
	};

	/**
	 *	The profiling system was setup in the expectation that there would be several
	 *	systems that each had its own island of profile information.  PhysX, client code,
	 *	and APEX would be the first examples of these.  Each one of these islands is represented
	 *	by a profile zone.
	 *	
	 *	The Manager is a singleton-like object where all these different systems can be registered
	 *	so that clients of the profiling system can have one point to capture *all* profiling events.
	 *
	 *	Flushing the manager implies that you want to loop through all the profile zones and flush
	 *	each one.
	 */
	class PxProfileZoneManager 
		: public PxProfileEventFlusher //Tell all SDK's to flush their queue of profile events.
	{
	protected:
		virtual ~PxProfileZoneManager(){}
	public:
		/**
		 *	Threadsafe call, can be done from any thread.  Handlers that are already connected
		 *	will get a new callback on the current thread.
		 */
		virtual void addProfileZone( PxProfileZone& inSDK ) = 0;
		virtual void removeProfileZone( PxProfileZone& inSDK ) = 0;

		/**
		 *	Threadsafe call.  The new handler will immediately be notified about all
		 *	known SDKs.
		 */
		virtual void addProfileZoneHandler( PxProfileZoneHandler& inHandler ) = 0;
		virtual void removeProfileZoneHandler( PxProfileZoneHandler& inHandler ) = 0;


		/**
		 *	Create a new profile zone.  This means you don't need access to a PxFoundation to 
		 *	create your profile zone object, and your object is automatically registered with
		 *	the profile zone manager.
		 *
		 *	You still need to release your object when you are finished with it.
		 *	\param inSDKName Name of the SDK object.
		 *	\param inProvider Option set of event id to name mappings.
		 *	\param inEventBufferByteSize rough maximum size of the event buffer.  May exceed this size
		 *		by sizeof one event.  When full an immediate call to all listeners is made.
		 */
		virtual PxProfileZone& createProfileZone( const char* inSDKName, PxProfileNames inNames = PxProfileNames(), PxU32 inEventBufferByteSize = 0x4000 /*16k*/ ) = 0;

		/**
		 * Deprecated form of the above function.
		 */ 
		virtual PxProfileZone& createProfileZone( const char* inSDKName, PxProfileNameProvider* inProvider = NULL, PxU32 inEventBufferByteSize = 0x4000 /*16k*/ ) = 0;

		virtual void setUserCustomProfiler(PxUserCustomProfiler* callback) = 0;

		virtual void release() = 0;
		
		static PxProfileZoneManager& createProfileZoneManager(PxFoundation* inFoundation );
	};

}

#endif // PX_PROFILE_ZONE_MANAGER_H

/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_SCOPED_EVENT_H
#define PX_PROFILE_SCOPED_EVENT_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"
#include "physxprofilesdk/PxProfileCompileTimeEventFilter.h"

namespace physx { namespace profile {

#define TO_PX_PROFILE_EVENT_ID( subsystem, eventId ) PxProfileEventId( SubsystemIds::subsystem, EventIds::subsystem##eventId );

	template<bool TEnabled, typename TBufferType>
	inline void startEvent( TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext )
	{
		if ( TEnabled && inBuffer ) inBuffer->startEvent( inId, inContext );
	}

	template<bool TEnabled, typename TBufferType>
	inline void stopEvent( TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext )
	{
		if ( TEnabled && inBuffer ) inBuffer->stopEvent( inId, inContext );
	}
	
	template<typename TBufferType>
	inline void startEvent( bool inEnabled, TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext )
	{
		if ( inEnabled && inBuffer ) inBuffer->startEvent( inId, inContext );
	}

	template<typename TBufferType>
	inline void stopEvent( bool inEnabled, TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext )
	{
		if ( inEnabled && inBuffer ) inBuffer->stopEvent( inId, inContext );
	}
	
	template<typename TBufferType>
	inline void eventValue( bool inEnabled, TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext, PxI64 inValue )
	{
		if ( inEnabled && inBuffer ) inBuffer->eventValue( inId, inContext, inValue );
	}

	template<bool TEnabled, typename TBufferType, PxU16 eventId>
	struct ScopedEventWithContext
	{
		PxU64				mContext;
		TBufferType*		mBuffer;
		ScopedEventWithContext( TBufferType* inBuffer, PxU64 inContext)
			: mContext ( inContext )
			, mBuffer( inBuffer )
		{
			startEvent<true>( mBuffer, PxProfileEventId(eventId), mContext );
		}
		~ScopedEventWithContext()
		{
			stopEvent<true>( mBuffer, PxProfileEventId(eventId), mContext );
		}
	};

	template<typename TBufferType, PxU16 eventId>
	struct ScopedEventWithContext<false,TBufferType,eventId> { ScopedEventWithContext( TBufferType*, PxU64) {} };

	template<typename TBufferType>
	struct DynamicallyEnabledScopedEvent
	{
		TBufferType*		mBuffer;
		PxProfileEventId	mId;
		PxU64				mContext;
		DynamicallyEnabledScopedEvent( TBufferType* inBuffer, const PxProfileEventId& inId, PxU64 inContext)
			: mBuffer( inBuffer )
			, mId( inId )
			, mContext( inContext )
		{
			startEvent( mId.mCompileTimeEnabled, mBuffer, mId, mContext );
		}
		~DynamicallyEnabledScopedEvent()
		{
			stopEvent( mId.mCompileTimeEnabled, mBuffer, mId, mContext );
		}
	};
}}

#define PX_PROFILE_SCOPED_EVENT_WITH_CONTEXT( TBufferType, subsystem, eventId, buffer, context ) \
	physx::profile::ScopedEventWithContext<PX_PROFILE_EVENT_FILTER_VALUE(subsystem,eventId), TBufferType, physx::profile::EventIds::subsystem##eventId> profileScopedEvent( buffer, context );

#define PX_PROFILE_EVENT_VALUE_WITH_CONTEXT( subsystem, eventId, buffer, context, value ) \
	eventValue( PX_PROFILE_EVENT_FILTER_VALUE(subsystem,eventId), buffer, physx::profile::EventIds::subsystem##eventId, context, value );

#endif // PX_PROFILE_SCOPED_EVENT_H

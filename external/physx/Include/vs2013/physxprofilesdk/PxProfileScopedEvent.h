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

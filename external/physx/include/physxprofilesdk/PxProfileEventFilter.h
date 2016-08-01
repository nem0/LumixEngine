/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_FILTER_H
#define PX_PROFILE_EVENT_FILTER_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"

namespace physx {

	//Called upon every event to give a quick-out before adding the event
	//to the event buffer.
	class PxProfileEventFilter
	{
	protected:
		virtual ~PxProfileEventFilter(){}
	public:
		//Disabled events will not go into the event buffer and will not be 
		//transmitted to clients.
		virtual void setEventEnabled( const PxProfileEventId& inId, bool isEnabled ) = 0;
		virtual bool isEventEnabled( const PxProfileEventId& inId ) const = 0;
	};

	//Forwards the filter requests to another event filter.
	template<typename TFilterType>
	struct PxProfileEventFilterForward
	{
		TFilterType* mFilter;
		PxProfileEventFilterForward( TFilterType* inFilter ) : mFilter( inFilter ) {}
		void setEventEnabled( const PxProfileEventId& inId, bool isEnabled ) { mFilter->setEventEnabled( inId, isEnabled ); }
		bool isEventEnabled( const PxProfileEventId& inId ) const { return mFilter->isEventEnabled( inId ); }
	};
	
	//Implements the event filter interface using another implementation
	template<typename TFilterType>
	struct PxProfileEventFilterImpl : public PxProfileEventFilter
	{
		PxProfileEventFilterForward<TFilterType> mFilter;
		PxProfileEventFilterImpl( TFilterType* inFilter ) : mFilter( inFilter ) {}
		virtual void setEventEnabled( const PxProfileEventId& inId, bool isEnabled ) { mFilter.setEventEnabled( inId, isEnabled ); }
		virtual bool isEventEnabled( const PxProfileEventId& inId ) const { return mFilter.isEventEnabled( inId ); }
	};

	//simple event filter that enables all events.
	struct PxProfileNullEventFilter
	{
		void setEventEnabled( const PxProfileEventId&, bool) { PX_ASSERT(false); }
		bool isEventEnabled( const PxProfileEventId&) const { return true; }
	};
}

#endif // PX_PROFILE_EVENT_FILTER_H

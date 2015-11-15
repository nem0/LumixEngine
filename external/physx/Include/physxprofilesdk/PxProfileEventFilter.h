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

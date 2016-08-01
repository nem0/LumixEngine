/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_ID_H
#define PX_PROFILE_EVENT_ID_H

#include "physxprofilesdk/PxProfileBase.h"

namespace physx
{
	/**
		A event id structure.  Optionally includes information about
		if the event was enabled at compile time.
	*/
	struct PxProfileEventId
	{
		PxU16			mEventId;
		mutable bool	mCompileTimeEnabled; 
		PxProfileEventId( PxU16 inId = 0, bool inCompileTimeEnabled = true )
			: mEventId( inId )
			, mCompileTimeEnabled( inCompileTimeEnabled )
		{
		}
		operator PxU16 () const { return mEventId; }
		bool operator==( const PxProfileEventId& inOther ) const 
		{ 
			return mEventId == inOther.mEventId;
		}
	};

	template<bool TEnabled>
	struct PxProfileCompileTimeFilteredEventId : public PxProfileEventId
	{
		PxProfileCompileTimeFilteredEventId( PxU16 inId = 0 )
			: PxProfileEventId( inId, TEnabled )
		{
		}
	};
}

#endif // PX_PROFILE_EVENT_ID_H

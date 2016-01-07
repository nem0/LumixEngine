/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef PX_PROFILE_EVENT_MUTEX_H
#define PX_PROFILE_EVENT_MUTEX_H

#include "physxprofilesdk/PxProfileBase.h"

namespace physx {
	
	/**
	 *	Mutex interface that hides implementation around lock and unlock.
	 *	The event system locks the mutex for every interaction.
	 */
	class PxProfileEventMutex
	{
	protected:
		virtual ~PxProfileEventMutex(){}
	public:
		virtual void lock() = 0;
		virtual void unlock() = 0;
	};

	/**
	 * Take any mutex type that implements lock and unlock and make an EventMutex out of it.
	 */
	template<typename TMutexType>
	struct PxProfileEventMutexImpl : public PxProfileEventMutex
	{
		TMutexType* mMutex;
		PxProfileEventMutexImpl( TMutexType* inMtx ) : mMutex( inMtx ) {}
		virtual void lock() { mMutex->lock(); }
		virtual void unlock() { mMutex->unlock(); }
	};

}

#endif // PX_PROFILE_EVENT_MUTEX_H

/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_CONTEXT_PROVIDER_H
#define PX_PROFILE_CONTEXT_PROVIDER_H

#include "physxprofilesdk/PxProfileBase.h"

namespace physx {

	struct PxProfileEventExecutionContext
	{
		PxU32					mThreadId;
		PxU8					mCpuId;
		PxU8					mThreadPriority;

		PxProfileEventExecutionContext( PxU32 inThreadId = 0, PxU8 inThreadPriority = 2 /*eThreadPriorityNormal*/, PxU8 inCpuId = 0 )
			: mThreadId( inThreadId )
			, mCpuId( inCpuId )
			, mThreadPriority( inThreadPriority )
		{
		}

		bool operator==( const PxProfileEventExecutionContext& inOther ) const
		{
			return mThreadId == inOther.mThreadId
				&& mCpuId == inOther.mCpuId
				&& mThreadPriority == inOther.mThreadPriority;
		}
	};

	//Provides the context in which the event is happening.
	class PxProfileContextProvider
	{
	protected:
		virtual ~PxProfileContextProvider(){}
	public:
		virtual PxProfileEventExecutionContext getExecutionContext() = 0;
		virtual PxU32 getThreadId() = 0;
	};
	//Provides pre-packaged context.
	struct PxProfileTrivialContextProvider
	{
		PxProfileEventExecutionContext mContext;
		PxProfileTrivialContextProvider( PxProfileEventExecutionContext inContext = PxProfileEventExecutionContext() )
			: mContext( inContext )
		{
		}
		PxProfileEventExecutionContext getExecutionContext() { return mContext; }
		PxU32 getThreadId() { return mContext.mThreadId; }
	};
	
	//Forwards the get context calls to another (perhaps shared) context.
	template<typename TProviderType>
	struct PxProfileContextProviderForward
	{
		TProviderType* mProvider;
		PxProfileContextProviderForward( TProviderType* inProvider ) : mProvider( inProvider ) {}
		PxProfileEventExecutionContext getExecutionContext() { return mProvider->getExecutionContext(); }
		PxU32 getThreadId() { return mProvider->getThreadId(); }
	};
	template<typename TProviderType>
	struct PxProfileContextProviderImpl : public PxProfileContextProvider
	{
		PxProfileContextProviderForward<TProviderType> mContext;
		PxProfileContextProviderImpl( TProviderType* inP ) : mContext( inP ) {}
		PxProfileEventExecutionContext getExecutionContext() { return mContext.getExecutionContext(); }
		PxU32 getThreadId() { return mContext.getThreadId(); }
	};
}

#endif // PX_PROFILE_CONTEXT_PROVIDER_H

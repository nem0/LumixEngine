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

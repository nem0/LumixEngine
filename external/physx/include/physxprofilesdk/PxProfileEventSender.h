/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef PX_PROFILE_EVENT_SENDER_H
#define PX_PROFILE_EVENT_SENDER_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileContextProvider.h"
#include "physxprofilesdk/PxProfileEventId.h"

namespace physx {

	/**
	 *	Tagging interface to indicate an object that is capable of flushing a profile
	 *	event stream at a certain point.
	 */
	class PxProfileEventFlusher
	{
	protected:
		virtual ~PxProfileEventFlusher(){}
	public:
		virtual void flushProfileEvents() = 0;
	};

	/**
	 *	Sends the full events where the caller must provide the context and thread id.
	 */
	class PxProfileEventSender
	{
	protected:
		virtual ~PxProfileEventSender(){}
	public:
	
		// use this as a thread id for events that start on one thread and end on another
		static const PxU32 CrossThreadId = 99999789;

		//Send a profile event, optionally with a context.  Events are sorted by thread
		//and context in the client side.
		virtual void startEvent( PxU16 inId, PxU64 contextId) = 0;
		virtual void stopEvent( PxU16 inId, PxU64 contextId) = 0;

		virtual void startEvent( PxU16 inId, PxU64 contextId, PxU32 threadId) = 0;
		virtual void stopEvent( PxU16 inId, PxU64 contextId, PxU32 threadId ) = 0;

		/**
		 *	Set an specific events value.  This is different than the profiling value
		 *	for the event; it is a value recorded and kept around without a timestamp associated
		 *	with it.  This value is displayed when the event itself is processed.
		 */
		virtual void eventValue( PxU16 inId, PxU64 contextId, PxI64 inValue ) = 0;

		//GPUProfile.h .. 
		/*		
			typedef struct CUDA_ALIGN_16
			{
				PxU16 block;
				PxU8  warp;
				PxU8  mpId;
				PxU8  hwWarpId;
				PxU8  userDataCfg;
				PxU16 eventId;
				PxU32 startTime;
				PxU32 endTime;
			} warpProfileEvent;
		*/
		static const PxU32 CurrentCUDABufferFormat = 1;

		/**
		 *	Send a CUDA profile buffer.  We assume the submit time is almost exactly the end time of the batch.
		 *	We then work backwards, using the batchRuntimeInMilliseconds in order to get the original time
		 *	of the batch.  The buffer format is described in GPUProfile.h.
		 *
		 *	\param batchRuntimeInMilliseconds The batch runtime in milliseconds, see cuEventElapsedTime.
		 *	\param cudaData An opaque pointer to the buffer of cuda data.
		 *	\param bufLenInBytes length of the cuda data buffer in bytes
		 *	\param bufferVersion Version of the format of the cuda data.
		 */
		virtual void CUDAProfileBuffer( PxF32 batchRuntimeInMilliseconds, const PxU8* cudaData, PxU32 bufLenInBytes, PxU32 bufferVersion = CurrentCUDABufferFormat ) = 0;
	};

	/**
		Tagging interface to indicate an object that may or may not return
		an object capable of adding profile events to a buffer.
	*/
	class PxProfileEventSenderProvider
	{
	protected:
		virtual ~PxProfileEventSenderProvider(){}
	public:
		/**
			This method can *always* return NULL.
			Thus need to always check that what you are getting is what
			you think it is.

			\return Perhaps a profile event sender.
		*/
		virtual PxProfileEventSender* getProfileEventSender() = 0;
	};
}

#endif // PX_PROFILE_EVENT_SENDER_H

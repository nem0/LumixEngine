/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifndef PX_DEFAULT_BUFFERED_PROFILER_H
#define PX_DEFAULT_BUFFERED_PROFILER_H
/** \addtogroup extensions
  @{
*/

#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	class PxProfileZoneManager;	

	/**
	\brief Event structure for buffered profiler callback. 

	\note This structure is used also for CUDA events, therefore some 
	members are not defined in case of a CUDA event.

	@see physx::PxBufferedProfilerCallback
	*/
	struct PxBufferedProfilerEvent
	{
		PxU64			startTimeNs;		//!< the event start time in nanoseconds
		PxU64			stopTimeNs;			//!< the event end time in nanoseconds
		const char*		name;				//!< the event name		
		const char*		profileZoneName;	//!< the name of the profile zone in which the event was generated
		PxU16			id;					//!< the event id
		PxU64			contextId;			//!< the optional contextId of the event set in start/endEvent, 
											//!< usually set to pointer inside the SDK. Not defined for CUDA events.
											//!< @see physx::PxProfileEventSender
		PxU32			threadId;			//!< the thread in which the event was executed. Not defined for CUDA events.
		PxU8			threadPriority;		//!< the priority of the thread in which the event was executed. Not defined for CUDA events.
		PxU8			cpuId;				//!< the CPU on which the event was executed. Not defined for CUDA events.
	};

	/**
	\brief A profiler callback that is called when the event buffer of a PxProfileZone fills up or is flushed.

	@see physx::PxProfileZone physx::PxProfileZoneManager physx::PxProfileZone
	*/
	class PxBufferedProfilerCallback
	{
	public:
		/**
		\brief Fixed ID for cross thread events.		
		*/
		static const PxU32 CROSS_THREAD_ID = 99999789;

		/**
		\brief Reports a start-stop event.		

		\param[out] event the reported event.

		@see PxBufferedProfilerEvent
		*/
		virtual void onEvent(const PxBufferedProfilerEvent &event) = 0;

	protected:
		virtual ~PxBufferedProfilerCallback(void) {}
	};

	//////////////////////////////////////////////////////////////////////////

	/**
	\brief Default implementation for profile event handler.

	The profile event handler listens for events from one or more profile zones, specified at creation time. It forwards those events
	to one or more callbacks.

	Events will be reported when internal event buffers fill up. Calling flushEvents() result
	 in any unreported events being reported immediately.

	@see PxDefaultBufferedProfilerCreate physx::PxProfileZone physx::PxProfileZoneMananger 
	*/
	class PxDefaultBufferedProfiler
	{
	public:
		/**
		\brief Flush all the event buffers to ensure that event callbacks see all events that have been issued.

		@see PxBufferedProfilerCallback
		*/		
		virtual void flushEvents() = 0;

		/**
		\brief Get the profile zone manager.
		*/
		virtual PxProfileZoneManager& getProfileZoneManager() = 0;

		/**
		\brief add an event callback.

		\param[in] cb the callback to add.
		*/
		virtual void addBufferedProfilerCallback(PxBufferedProfilerCallback& cb) = 0;

		/**
		\brief remove an event callback.

		\param[in] cb the event callback to remove.
		*/
		virtual void removeBufferedProfilerCallback(PxBufferedProfilerCallback& cb) = 0;

		/**
		\brief Release the PxDefaultBufferedProfiler.
		*/
		virtual void release() = 0;

	protected:
		virtual ~PxDefaultBufferedProfiler(void) {}
	};


	/**
	\brief Create default PxDefaultBufferedProfiler.

	Create a default buffered profiler.

	\param[in] foundation PxFoundation used for PxProfileZoneManager creation.
	\param[in] profileZoneNames Space-separated names of PxProfileZones for which events should be reported to the callback.

	\note Example usage: PxDefaultBufferedProfilerCreate(*gFoundation, "PhysXSDK PxTaskManager");
	\note List of PhysX SDK profile zone names: PhysXSDK, PxTaskManager, PxGpuDispatcher (for CUDA GPU events)

	@see PxDefaultBufferedProfiler physx::PxProfileZoneManager physx::PxProfileZone
	*/
	PxDefaultBufferedProfiler* PxDefaultBufferedProfilerCreate(PxFoundation	& foundation, const char * profileZoneNames );


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_DEFAULT_BUFFERED_PROFILER_H

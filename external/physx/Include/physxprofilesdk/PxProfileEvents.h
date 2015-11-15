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

#ifndef PX_PROFILE_EVENTS_H
#define PX_PROFILE_EVENTS_H

#include "physxprofilesdk/PxProfileBase.h"
#include "physxprofilesdk/PxProfileEventId.h"

#define	UNION_1(a)						physx::profile::TUnion<a, physx::profile::Empty>
#define	UNION_2(a,b)					physx::profile::TUnion<a, UNION_1(b)>
#define	UNION_3(a,b,c)					physx::profile::TUnion<a, UNION_2(b,c)>
#define	UNION_4(a,b,c,d)				physx::profile::TUnion<a, UNION_3(b,c,d)>
#define	UNION_5(a,b,c,d,e)				physx::profile::TUnion<a, UNION_4(b,c,d,e)>
#define	UNION_6(a,b,c,d,e,f)			physx::profile::TUnion<a, UNION_5(b,c,d,e,f)>
#define	UNION_7(a,b,c,d,e,f,g)			physx::profile::TUnion<a, UNION_6(b,c,d,e,f,g)>
#define	UNION_8(a,b,c,d,e,f,g,h)		physx::profile::TUnion<a, UNION_7(b,c,d,e,f,g,h)>
#define	UNION_9(a,b,c,d,e,f,g,h,i)		physx::profile::TUnion<a, UNION_8(b,c,d,e,f,g,h,i)>

namespace physx { namespace profile {

	struct Empty {};

	template <typename T> struct Type2Type {};

	template <typename U, typename V>
	union TUnion
	{
		typedef U Head;
		typedef V Tail;

		Head	head;
		Tail	tail;

		template <typename TDataType>
		void init(const TDataType& inData)
		{
			toType(Type2Type<TDataType>()).init(inData);
		}

		template <typename TDataType>
		PX_FORCE_INLINE TDataType& toType(const Type2Type<TDataType>& outData) { return tail.toType(outData); }

		PX_FORCE_INLINE Head& toType(const Type2Type<Head>&) { return head; }

		template <typename TDataType>
		PX_FORCE_INLINE const TDataType& toType(const Type2Type<TDataType>& outData) const { return tail.toType(outData); }

		PX_FORCE_INLINE const Head& toType(const Type2Type<Head>&) const { return head; }
	};

	struct EventTypes
	{
		enum Enum
		{
			Unknown = 0,
			StartEvent,
			StopEvent,
			RelativeStartEvent, //reuses context,id from the earlier event.
			RelativeStopEvent, //reuses context,id from the earlier event.
			EventValue,
			CUDAProfileBuffer
		};
	};

	struct EventStreamCompressionFlags
	{
		enum Enum
		{
			U8 = 0,
			U16 = 1,
			U32 = 2,
			U64 = 3,
			CompressionMask = 3
		};
	};


	//Find the smallest value that will represent the incoming value without loss.
	//We can enlarge the current compression value, but we can't make is smaller.
	//In this way, we can use this function to find the smallest compression setting
	//that will work for a set of values.
	inline EventStreamCompressionFlags::Enum findCompressionValue( PxU64 inValue, EventStreamCompressionFlags::Enum inCurrentCompressionValue = EventStreamCompressionFlags::U8 )
	{
		//Fallthrough is intentional
		switch( inCurrentCompressionValue ) 
		{
		case EventStreamCompressionFlags::U8:
			if ( inValue <= PX_MAX_U8 )
				return EventStreamCompressionFlags::U8;
		case EventStreamCompressionFlags::U16:
			if ( inValue <= PX_MAX_U16 )
				return EventStreamCompressionFlags::U16;
		case EventStreamCompressionFlags::U32:
			if ( inValue <= PX_MAX_U32 )
				return EventStreamCompressionFlags::U32;
		case EventStreamCompressionFlags::U64:
		default:
			return EventStreamCompressionFlags::U64;
		}
	}

	//Find the smallest value that will represent the incoming value without loss.
	//We can enlarge the current compression value, but we can't make is smaller.
	//In this way, we can use this function to find the smallest compression setting
	//that will work for a set of values.
	inline EventStreamCompressionFlags::Enum findCompressionValue( PxU32 inValue, EventStreamCompressionFlags::Enum inCurrentCompressionValue = EventStreamCompressionFlags::U8 )
	{
		//Fallthrough is intentional
		switch( inCurrentCompressionValue ) 
		{
		case EventStreamCompressionFlags::U8:
			if ( inValue <= PX_MAX_U8 )
				return EventStreamCompressionFlags::U8;
		case EventStreamCompressionFlags::U16:
			if ( inValue <= PX_MAX_U16 )
				return EventStreamCompressionFlags::U16;
		case EventStreamCompressionFlags::U32:
		case EventStreamCompressionFlags::U64:
		default:
			return EventStreamCompressionFlags::U32;
		}
	}
	
	//Event header is 32 bytes and precedes all events.
	struct EventHeader
	{
		PxU8	mEventType; //Used to parse the correct event out of the stream
		PxU8	mStreamOptions; //Timestamp compression, etc.
		PxU16	mEventId;	//16 bit per-event-system event id
		EventHeader( PxU8 type = 0, PxU16 id = 0 )
			: mEventType( type )
			, mStreamOptions( (PxU8)-1 )
			, mEventId( id )
		{
		}

		EventHeader( EventTypes::Enum type, PxU16 id )
			: mEventType( static_cast<PxU8>( type ) )
			, mStreamOptions( (PxU8)-1 )
			, mEventId( id )
		{
		}

		EventStreamCompressionFlags::Enum getTimestampCompressionFlags() const 
		{ 
			return static_cast<EventStreamCompressionFlags::Enum> ( mStreamOptions & EventStreamCompressionFlags::CompressionMask );
		}

		PxU64 compressTimestamp( PxU64 inLastTimestamp, PxU64 inCurrentTimestamp )
		{
			mStreamOptions = EventStreamCompressionFlags::U64;
			PxU64 retval = inCurrentTimestamp;
			if ( inLastTimestamp )
			{
				retval = inCurrentTimestamp - inLastTimestamp;
				EventStreamCompressionFlags::Enum compressionValue = findCompressionValue( retval );
				mStreamOptions = static_cast<PxU8>( compressionValue );
				if ( compressionValue == EventStreamCompressionFlags::U64 )
					retval = inCurrentTimestamp; //just send the timestamp as is.
			}
			return retval;
		}

		PxU64 uncompressTimestamp( PxU64 inLastTimestamp, PxU64 inCurrentTimestamp ) const
		{
			if ( getTimestampCompressionFlags() != EventStreamCompressionFlags::U64 )
				return inLastTimestamp + inCurrentTimestamp;
			return inCurrentTimestamp;
		}

		void setContextIdCompressionFlags( PxU64 inContextId )
		{
			PxU8 options = static_cast<PxU8>( findCompressionValue( inContextId ) );
			mStreamOptions = PxU8(mStreamOptions | options << 2);
		}

		EventStreamCompressionFlags::Enum getContextIdCompressionFlags() const 
		{
			return static_cast< EventStreamCompressionFlags::Enum >( ( mStreamOptions >> 2 ) & EventStreamCompressionFlags::CompressionMask );
		}

		bool operator==( const EventHeader& inOther ) const
		{
			return mEventType == inOther.mEventType
				&& mStreamOptions == inOther.mStreamOptions
				&& mEventId == inOther.mEventId;
		}

		template<typename TStreamType>
		inline void streamify( TStreamType& inStream )
		{
			inStream.streamify( "EventType", mEventType ); 
			inStream.streamify( "StreamOptions", mStreamOptions ); //Timestamp compression, etc.
			inStream.streamify( "EventId", mEventId );	//16 bit per-event-system event id
		}
	};

	//Declaration of type level getEventType function that maps enumeration event types to datatypes
	template<typename TDataType>
	inline EventTypes::Enum getEventType() { PX_ASSERT( false ); return EventTypes::Unknown; }

	//Relative profile event means this event is sharing the context and thread id
	//with the event before it.
	struct RelativeProfileEvent
	{
		PxU64	mTensOfNanoSeconds; //timestamp is in tensOfNanonseconds
		void init( PxU64 inTs ) { mTensOfNanoSeconds = inTs; }
		void init( const RelativeProfileEvent& inData ) { mTensOfNanoSeconds = inData.mTensOfNanoSeconds; }
		bool operator==( const RelativeProfileEvent& other ) const 
		{ 
			return mTensOfNanoSeconds == other.mTensOfNanoSeconds;
		}
		template<typename TStreamType> 
		void streamify( TStreamType& inStream, const EventHeader& inHeader )
		{
			inStream.streamify( "TensOfNanoSeconds", mTensOfNanoSeconds, inHeader.getTimestampCompressionFlags() );
		}
		PxU64 getTimestamp() const { return mTensOfNanoSeconds; }
		void setTimestamp( PxU64 inTs ) { mTensOfNanoSeconds = inTs; }
		void setupHeader( EventHeader& inHeader, PxU64 inLastTimestamp )
		{
			mTensOfNanoSeconds = inHeader.compressTimestamp( inLastTimestamp, mTensOfNanoSeconds );
		}
	};

	//Start version of the relative event.
	struct RelativeStartEvent : public RelativeProfileEvent
	{
		void init( PxU64 inTs = 0 ) { RelativeProfileEvent::init( inTs ); }
		void init( const RelativeStartEvent& inData ) { RelativeProfileEvent::init( inData ); }
		template<typename THandlerType>
		void handle( THandlerType* inHdlr, PxU16 eventId, PxU32 thread, PxU64 context, PxU8 inCpuId, PxU8 threadPriority ) const
		{
			inHdlr->onStartEvent( PxProfileEventId( eventId ), thread, context, inCpuId, threadPriority, mTensOfNanoSeconds );
		}
	};
	
	template<> inline EventTypes::Enum getEventType<RelativeStartEvent>() { return EventTypes::RelativeStartEvent; }
	
	//Stop version of relative event.
	struct RelativeStopEvent : public RelativeProfileEvent
	{
		void init( PxU64 inTs = 0 ) { RelativeProfileEvent::init( inTs ); }
		void init( const RelativeStopEvent& inData ) { RelativeProfileEvent::init( inData ); }
		template<typename THandlerType>
		void handle( THandlerType* inHdlr, PxU16 eventId, PxU32 thread, PxU64 context, PxU8 inCpuId, PxU8 threadPriority ) const
		{
			inHdlr->onStopEvent( PxProfileEventId( eventId ), thread, context, inCpuId, threadPriority, mTensOfNanoSeconds );
		}
	};

	template<> inline EventTypes::Enum getEventType<RelativeStopEvent>() { return EventTypes::RelativeStopEvent; }

	struct EventContextInformation
	{
		PxU64 mContextId;
		PxU32 mThreadId; //Thread this event was taken from
		PxU8  mThreadPriority;
		PxU8  mCpuId;

		void init( PxU32 inThreadId = PX_MAX_U32
								, PxU64 inContextId = ((PxU64) -1)
								, PxU8 inPriority = PX_MAX_U8
								, PxU8 inCpuId = PX_MAX_U8 )
		{
			mContextId = inContextId;
			mThreadId = inThreadId;
			mThreadPriority = inPriority;
			mCpuId = inCpuId;
		}

		void init( const EventContextInformation& inData )
		{
			mContextId = inData.mContextId;
			mThreadId = inData.mThreadId;
			mThreadPriority = inData.mThreadPriority;
			mCpuId = inData.mCpuId;
		}

		template<typename TStreamType> 
		void streamify( TStreamType& inStream, EventStreamCompressionFlags::Enum inContextIdFlags )
		{
			inStream.streamify( "ThreadId", mThreadId );
			inStream.streamify( "ContextId", mContextId, inContextIdFlags );
			inStream.streamify( "ThreadPriority", mThreadPriority );
			inStream.streamify( "CpuId", mCpuId );
		}
		
		bool operator==( const EventContextInformation& other ) const 
		{ 
			return mThreadId == other.mThreadId
				&& mContextId == other.mContextId
				&& mThreadPriority == other.mThreadPriority
				&& mCpuId == other.mCpuId;
		}

		void setToDefault()
		{
			*this = EventContextInformation();
		}
	};
	
	//Profile event contains all the data required to tell the profile what is going
	//on.
	struct ProfileEvent
	{
		EventContextInformation mContextInformation;
		RelativeProfileEvent	mTimeData; //timestamp in seconds.
		void init( PxU32 inThreadId, PxU64 inContextId, PxU8 inCpuId, PxU8 inPriority, PxU64 inTs )
		{
			mContextInformation.init( inThreadId, inContextId, inPriority, inCpuId );
			mTimeData.init( inTs );
		}

		void init( const ProfileEvent& inData )
		{
			mContextInformation.init( inData.mContextInformation );
			mTimeData.init( inData.mTimeData );
		}

		bool operator==( const ProfileEvent& other ) const 
		{ 
			return mContextInformation == other.mContextInformation
					&& mTimeData == other.mTimeData; 
		}

		template<typename TStreamType> 
		void streamify( TStreamType& inStream, const EventHeader& inHeader )
		{
			mContextInformation.streamify( inStream, inHeader.getContextIdCompressionFlags() );
			mTimeData.streamify( inStream, inHeader );
		}

		PxU64 getTimestamp() const { return mTimeData.getTimestamp(); }
		void setTimestamp( PxU64 inTs ) { mTimeData.setTimestamp( inTs ); }
		
		void setupHeader( EventHeader& inHeader, PxU64 inLastTimestamp )
		{
			mTimeData.setupHeader( inHeader, inLastTimestamp );
			inHeader.setContextIdCompressionFlags( mContextInformation.mContextId );
		}
	};

	//profile start event starts the profile session.
	struct StartEvent : public ProfileEvent
	{
		void init( PxU32 inThreadId = 0, PxU64 inContextId = 0, PxU8 inCpuId = 0, PxU8 inPriority = 0, PxU64 inTensOfNanoSeconds = 0 ) 
		{
			ProfileEvent::init( inThreadId, inContextId, inCpuId, inPriority, inTensOfNanoSeconds );
		}
		void init( const StartEvent& inData )
		{
			ProfileEvent::init( inData );
		}

		RelativeStartEvent getRelativeEvent() const { RelativeStartEvent theEvent; theEvent.init( mTimeData.mTensOfNanoSeconds ); return theEvent; }
		EventTypes::Enum getRelativeEventType() const { return getEventType<RelativeStartEvent>(); }
	};
	
	template<> inline EventTypes::Enum getEventType<StartEvent>() { return EventTypes::StartEvent; }

	//Profile stop event stops the profile session.
	struct StopEvent : public ProfileEvent
	{
		void init( PxU32 inThreadId = 0, PxU64 inContextId = 0, PxU8 inCpuId = 0, PxU8 inPriority = 0, PxU64 inTensOfNanoSeconds = 0 )
		{
			ProfileEvent::init( inThreadId, inContextId, inCpuId, inPriority, inTensOfNanoSeconds );
		}
		void init( const StopEvent& inData )
		{
			ProfileEvent::init( inData );
		}
		RelativeStopEvent getRelativeEvent() const { RelativeStopEvent theEvent; theEvent.init( mTimeData.mTensOfNanoSeconds ); return theEvent; }
		EventTypes::Enum getRelativeEventType() const { return getEventType<RelativeStopEvent>(); }
	};
	
	template<> inline EventTypes::Enum getEventType<StopEvent>() { return EventTypes::StopEvent; }

	struct EventValue
	{
		PxU64	mValue;
		PxU64	mContextId;
		PxU32	mThreadId;
		void init( PxI64 inValue = 0, PxU64 inContextId = 0, PxU32 inThreadId = 0 )
		{
			mValue = static_cast<PxU64>( inValue );
			mContextId = inContextId;
			mThreadId = inThreadId;
		}

		void init( const EventValue& inData )
		{
			mValue = inData.mValue;
			mContextId = inData.mContextId;
			mThreadId = inData.mThreadId;
		}

		PxI64 getValue() const { return static_cast<PxI16>( mValue ); }

		void setupHeader( EventHeader& inHeader )
		{
			mValue = inHeader.compressTimestamp( 0, mValue );
			inHeader.setContextIdCompressionFlags( mContextId );
		}

		template<typename TStreamType> 
		void streamify( TStreamType& inStream, const EventHeader& inHeader )
		{
			inStream.streamify( "Value", mValue, inHeader.getTimestampCompressionFlags() );
			inStream.streamify( "ContextId", mContextId, inHeader.getContextIdCompressionFlags() );
			inStream.streamify( "ThreadId", mThreadId );
		}

		bool operator==( const EventValue& other ) const 
		{ 
			return mValue == other.mValue
				&& mContextId == other.mContextId
				&& mThreadId == other.mThreadId;
		}

		template<typename THandlerType>
		void handle( THandlerType* inHdlr, PxU16 eventId ) const
		{
			inHdlr->onEventValue( PxProfileEventId( eventId ), mThreadId, mContextId, getValue() );
		}

	};
	template<> inline EventTypes::Enum getEventType<EventValue>() { return EventTypes::EventValue; }

	struct CUDAProfileBuffer
	{
		PxU64 mTimestamp;
		PxF32 mTimespan;
		const PxU8* mCudaData;
		PxU32 mBufLen;
		PxU32 mVersion;

		void init( PxU64 timestamp = 0, PxF32 span = 0, const PxU8* cdata= 0, PxU32 buflen= 0, PxU32 version= 0 )
		{
			mTimestamp = timestamp;
			mTimespan = span;
			mCudaData = cdata;
			mBufLen = buflen;
			mVersion = version;
		}

		void init( const CUDAProfileBuffer& inData )
		{
			mTimestamp = inData.mTimestamp;
			mTimespan = inData.mTimespan;
			mCudaData = inData.mCudaData;
			mBufLen = inData.mBufLen;
			mVersion = inData.mVersion;
		}

		template<typename TStreamType> 
		void streamify( TStreamType& inStream, const EventHeader& )
		{
			inStream.streamify( "Timestamp", mTimestamp );
			inStream.streamify( "Timespan", mTimespan );
			inStream.streamify( "CudaData", mCudaData, mBufLen );
			inStream.streamify( "BufLen", mBufLen );
			inStream.streamify( "Version", mVersion );
		}

		bool operator==( const CUDAProfileBuffer& other ) const 
		{ 
			return mTimestamp == other.mTimestamp
				&& mTimespan == other.mTimespan
				&& mBufLen == other.mBufLen
				&& memcmp( mCudaData, other.mCudaData, mBufLen ) == 0
				&& mVersion == other.mVersion;
		}

		template<typename THandlerType>
		void handle( THandlerType* inHdlr ) const
		{
			inHdlr->onCUDAProfileBuffer( mTimestamp, mTimespan, mCudaData, mBufLen, mVersion );
		}
	};
	
	template<> inline EventTypes::Enum getEventType<CUDAProfileBuffer>() { return EventTypes::CUDAProfileBuffer; }

	//Provides a generic equal operation for event data objects.
	template <typename TEventData>
	struct EventDataEqualOperator
	{
		TEventData mData;
		EventDataEqualOperator( const TEventData& inD ) : mData( inD ) {}
		template<typename TDataType> bool operator()( const TDataType& inRhs ) const { return mData.toType( Type2Type<TDataType>() ) == inRhs; }
		bool operator()() const { return false; }
	};

	/**
	 *	Generic event container that combines and even header with the generic event data type.
	 *	Provides unsafe and typesafe access to the event data.
	 */
	class Event
	{
	public:
		typedef UNION_7(StartEvent, StopEvent, RelativeStartEvent, RelativeStopEvent, EventValue, CUDAProfileBuffer, PxU8) EventData;

	private:
		EventHeader mHeader;
		EventData	mData;
	public:
		Event() {}

		template <typename TDataType>
		Event( EventHeader inHeader, const TDataType& inData )
			: mHeader( inHeader )
		{
			mData.init<TDataType>(inData);
		}

		template<typename TDataType>
		Event( PxU16 eventId, const TDataType& inData )
			: mHeader( getEventType<TDataType>(), eventId )
		{
			mData.init<TDataType>(inData);
		}
		const EventHeader& getHeader() const { return mHeader; }
		const EventData& getData() const { return mData; }

		template<typename TDataType>
		const TDataType& getValue() const { PX_ASSERT( mHeader.mEventType == getEventType<TDataType>() ); return mData.toType<TDataType>(); }

		template<typename TDataType>
		TDataType& getValue() { PX_ASSERT( mHeader.mEventType == getEventType<TDataType>() ); return mData.toType<TDataType>(); }

		template<typename TRetVal, typename TOperator>
		inline TRetVal visit( TOperator inOp ) const;

		bool operator==( const Event& inOther ) const
		{
			if ( !(mHeader == inOther.mHeader ) ) return false;
			if ( mHeader.mEventType )
				return inOther.visit<bool>( EventDataEqualOperator<EventData>( mData ) );
			return true;
		}
	};

	//Combining the above union type with an event type means that an object can get the exact
	//data out of the union.  Using this function means that all callsites will be forced to
	//deal with the newer datatypes and that the switch statement only exists in once place.
	//Implements conversion from enum -> datatype
	template<typename TRetVal, typename TOperator>
	TRetVal visit( EventTypes::Enum inEventType, const Event::EventData& inData, TOperator inOperator )
	{
		switch( inEventType )
		{
		case EventTypes::StartEvent:			return inOperator( inData.toType( Type2Type<StartEvent>() ) );
		case EventTypes::StopEvent:				return inOperator( inData.toType( Type2Type<StopEvent>() ) );
		case EventTypes::RelativeStartEvent:	return inOperator( inData.toType( Type2Type<RelativeStartEvent>() ) );
		case EventTypes::RelativeStopEvent:		return inOperator( inData.toType( Type2Type<RelativeStopEvent>() ) );
		case EventTypes::EventValue:			return inOperator( inData.toType( Type2Type<EventValue>() ) );
		case EventTypes::CUDAProfileBuffer:		return inOperator( inData.toType( Type2Type<CUDAProfileBuffer>() ) );
		case EventTypes::Unknown:
		default: 								return inOperator( static_cast<PxU8>( inEventType ) );
		}
	}

	template<typename TRetVal, typename TOperator>
	inline TRetVal Event::visit( TOperator inOp ) const
	{ 
		return physx::profile::visit<TRetVal>( static_cast<EventTypes::Enum>(mHeader.mEventType), mData, inOp ); 
	}
} }

#endif // PX_PROFILE_EVENTS_H

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
#ifndef PVD_NETWORK_STREAMS_H
#define PVD_NETWORK_STREAMS_H
#include "foundation/PxSimpleTypes.h"
#include "physxvisualdebuggersdk/PvdErrorCodes.h"

namespace physx { namespace debugger {
	
	/**
	 *	Implementations don't need to implement a caching layer nor do they need
	 *	to worry about threadsafe implementations; that is all built on top of
	 *	this interface.
	 */
	class PvdNetworkOutStream
	{
	protected:
		virtual ~PvdNetworkOutStream(){}

	public:
		/**
		 *	write bytes to the other endpoint of the connection.  If an error occurs
		 *	this connection will assume to be dead.
		 *	
		 *	Errors -
		 *	
		 *	NetworkError - Some failure to write data either to buffer or over network.
		 */
		virtual PvdError write( const PxU8* inBytes, PxU32 inLength ) = 0;
		template<typename TDataType>
		PvdError write( const TDataType* data, PxU32 numItems )
		{
			return write( reinterpret_cast<const PxU8*>( data ), numItems * sizeof( TDataType ) );
		}
		
		/**
		 *	Return true if this stream is still connected.
		 */
		virtual bool isConnected() const = 0;
		/**
		 *	Close the in stream.
		 */
		virtual void disconnect() = 0;
		/**
		 *	release any resources related to this stream.
		 */
		virtual void release() = 0;

		/**
		 *	send any data and block until we know it is at least on the wire.
		 */
		virtual PvdError flush() = 0;

		/**
		*	Return the size of data have been written to target
		*/
		virtual PxU64 getWrittenDataSize() = 0;

		static PvdNetworkOutStream& createDoubleBuffered( PxAllocatorCallback& alloc, PvdNetworkOutStream& stream, PxU32 bufSize );
		static PvdNetworkOutStream* createFromFile( PxAllocatorCallback& alloc, const char* fname );
	};

	/**
	 *	Implementations don't need to implement a caching layer nor do they need
	 *	to worry about threadsafe implementations; that is all built on top of
	 *	this interface.
	 */
	class PvdNetworkInStream
	{
	protected:
		virtual ~PvdNetworkInStream(){}

	public:
		/**
		 *	Read the requested number of bytes from the socket.  Block until that number
		 *	of bytes is returned.
		 *	
		 *	Errors -
		 *	NetworkError - If call cannot complete.
		 */
		virtual PvdError readBytes( PxU8* outBytes, PxU32 ioRequested ) = 0;

		/**
		 *	Return true if this stream is still connected.
		 */
		virtual bool isConnected() const = 0;
		/**
		 *	Close the in stream.
		 */
		virtual void disconnect() = 0;
		/**
		 *	release any resources related to this stream.
		 */
		virtual void release() = 0;

		/**
		 *	Return the number of bytes the stream has read.
		 */
		virtual PxU64 getLoadedDataSize() = 0;
	};

	//Create an object responsible for a pair of instream/outstream
	//where instream may or may not exist.
	class PvdNetworkStreamOwner
	{
	protected:
		virtual ~PvdNetworkStreamOwner(){}
	public:
		virtual void addRef() = 0;
		virtual void release() = 0;
		//Calling destroy on these streams is equivalent to calling
		//release on this object.
		virtual PvdNetworkOutStream& lock() = 0;
		virtual void unlock() = 0;
		virtual PvdNetworkInStream* getInStream() = 0;

		static PvdNetworkStreamOwner& create( PxAllocatorCallback& alloc, PvdNetworkOutStream& outStream, PvdNetworkInStream* inStream );
	};

	class PvdNetworkStreams
	{
	public:
		static bool connect( PxAllocatorCallback& allocator 
					, const char* inHost
					, int inPort
					, unsigned int inTimeoutInMilliseconds
					, PvdNetworkInStream*& outInStream
					, PvdNetworkOutStream*& outOutStream );
	};
}}

#endif

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
#ifndef PVD_CONNECTION_MANAGER_H
#define PVD_CONNECTION_MANAGER_H

#include "physxvisualdebuggersdk/PvdConnectionFlags.h"
#include "physxvisualdebuggersdk/PvdNetworkStreams.h"
#include "physxvisualdebuggersdk/PvdConnection.h"
#include "foundation/PxErrors.h"

namespace physx {
	class PxProfileZoneManager;
}

namespace physx { namespace debugger {

	class PvdNetworkInStream;
	class PvdNetworkOutStream;
	struct PvdColor;
	typedef const char* String;
}}

namespace physx { namespace debugger { namespace comm {

	/**
	 *	When PVD is connected two callbacks happen.  This avoids conditions
	 *	where a handler tries to initiate sending objects from another
	 *	handler on its OnConnection but the first handler hasn't had an
	 *	opportunity to send its class descriptions.
	 *	The callbacks happen in this order:
	 *	onSendClassDescriptions
	 *	onPvdConnected
	 *	onPvdDisconnected
	 */
	//
	//
	class PvdConnectionHandler
	{
	protected:
		virtual ~PvdConnectionHandler(){}
	public:
		virtual void onPvdSendClassDescriptions( PvdConnection& connection ) = 0;
		virtual void onPvdConnected( PvdConnection& connection ) = 0;
		virtual void onPvdDisconnected( PvdConnection& connection ) = 0;
	};

	/**
	 *	The connection factory manager provides ways of managing a single PVD connection.
	 *	Clients can be notified when the connection is created and can setup a policy
	 *	for dealing with the incoming data from the PVD application if there is any.
	 *
	 *	The default data provider uses a thread that does a block read on the incoming
	 *	connection stream.  If you would like to do something else you will need first
	 *	implement you own network abstraction as the physx networking layers don't work
	 *	in non-blocking mode on platforms other than windows (and they only partially work
	 *	in non-blocking mode on windows).  
	 */
	class PvdConnectionManager
	{
	protected:
		virtual ~PvdConnectionManager(){}
	public:
		/**
		 *	Set the profile zone manager.  This takes care of ensuring that all profiling
		 *	events get forwarded to PVD.
		 */
		virtual void setProfileZoneManager( physx::PxProfileZoneManager& inManager ) = 0;
		
		//These will automatically get called on an active connection
		//so you don't need to worry about it.
		virtual void setPickable( const void* instance, bool pickable ) = 0;
		virtual void setColor( const void* instance, const PvdColor& color ) = 0;
		virtual void setCamera( String name, const PxVec3& position, const PxVec3& up, const PxVec3& target ) = 0;

		//Send error message to PVD
		virtual void sendErrorMessage(PxErrorCode::Enum code, String message, String file, PxU32 line) = 0;

		//Is top level indicates that this object will be shown at the root of the object graph
		//in the AllObjects display.  The only object this should be set for would be the
		//PhysX SDK object.  All other objects this should be false.
		virtual void setIsTopLevelUIElement( const void* instance, bool isTopLevel ) = 0;

		/*
		send a stream end event to pvd, pvd will do disconnect and store data when received this event.
		*/
		virtual void sendStreamEnd() = 0;

		/**
		 *	Handler will be notified every time there is a new connection.
		 */
		virtual void addHandler( PvdConnectionHandler& inHandler ) = 0;
		/**
		 *	Handler will be notified when a connection is destroyed.
		 */
		virtual void removeHandler( PvdConnectionHandler& inHandler ) = 0;
		/**
		 *	Create a new PvdConnection and returns the interface with an extra reference.
		 *
		 *	The connection type is static and can't change once the system starts.
		 *	Not that something could have disconnected by the time this function returned.
		 *	Such behavior would probably crash but it could happen. User need to release 
		 *  the returned interface after using it.
		 */
		virtual PvdConnection* connectAddRef( PvdNetworkInStream* inInStream
												, PvdNetworkOutStream& inOutStream
												, TConnectionFlagsType inConnectionType = defaultConnectionFlags()
												, bool doubleBuffered = true ) = 0;

		void connect( PvdNetworkInStream* inInStream
												, PvdNetworkOutStream& inOutStream
												, TConnectionFlagsType inConnectionType = defaultConnectionFlags()
												, bool doubleBuffered = true )
		{
			PvdConnection* theConnection = connectAddRef( inInStream, inOutStream, inConnectionType, doubleBuffered );
			if ( NULL != theConnection )
				theConnection->release();
		}

		//connect to PVD over the network
		void connect( PxAllocatorCallback& allocator
										, const char* inHost
										, int inPort
										, unsigned int inTimeoutInMilliseconds
										, TConnectionFlagsType inConnectionType = defaultConnectionFlags()
										, bool doubleBuffered = true )
		{
			PvdNetworkInStream* theInStream;
			PvdNetworkOutStream* theOutStream;
			if ( PvdNetworkStreams::connect( allocator, inHost, inPort, inTimeoutInMilliseconds, theInStream, theOutStream ) )
				connect( theInStream, *theOutStream, inConnectionType, doubleBuffered );
		}

		//connect to PVD over the filesystem
		void connect( PxAllocatorCallback& allocator, 
									const char* inFilename
									, TConnectionFlagsType inConnectionType = defaultConnectionFlags()
									, bool doubleBuffered = true)
		{
			PvdNetworkOutStream* fileStream = PvdNetworkOutStream::createFromFile( allocator, inFilename );
			if ( fileStream )
				connect( NULL, *fileStream, inConnectionType, doubleBuffered );
		}

		/**
		 *	Return the object representing the current connection to PVD, if any.
		 *  You need to call release on the connection after this call.  This is because
		 *	...
		 *	The manager releases its reference to the connection when something causes
		 *	the connection to disconnect.  This doesn't necessarily happen in this thread.
		 *	So in order to return a connection and be sure it isn't returning a dangling pointer
		 *	because another thread caused a disconnect (like because the read thread noticed
		 *	the socket is dead), this object addrefs the connection if possible and the returns
		 *	it.  So, callers of this function need to release the (possibly disconnected connection)
		 *	if they got one.
		 */
		virtual PvdConnection* getAndAddRefCurrentConnection() = 0;
		/**
		 * For the reasons stated above, querying isConnected is an atomic
		 * operation.
		 */
		virtual bool isConnected() = 0;

		/**
		 *	If there is a current connection, disconnect from the factory.
		 */
		virtual void disconnect() = 0;

		virtual void release() = 0;

		static PvdConnectionManager& create( PxAllocatorCallback& allocator, PxAllocatorCallback& nonBroadcastingAlloc, bool trackMemoryEvents = true );
	};

}}

/** \brief Convenience typedef for the PvdConnectionHandler. */
typedef debugger::comm::PvdConnectionHandler PxVisualDebuggerConnectionHandler;

/** \brief Convenience typedef for the PvdConnectionManager. */
typedef debugger::comm::PvdConnectionManager PxVisualDebuggerConnectionManager;
}

#endif

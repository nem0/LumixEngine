/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_CONNECTION_H
#define PVD_CONNECTION_H
#include "physxvisualdebuggersdk/PvdConnectionFlags.h"

namespace physx { namespace debugger {
	class PvdNetworkOutStream;
}}

namespace physx { namespace debugger { namespace renderer {
	class PvdUserRenderer;	
}}}

namespace physx { namespace debugger { namespace comm {

	class PvdMetaDataStream;
	class PvdDataStream;
	
	class PvdConnection
	{
	protected:
		virtual ~PvdConnection(){}
	public:
		virtual void addRef() = 0;
		virtual void release() = 0;

		//A data stream is not threadsafe, although you can use multiple
		//data streams, each used from only one thread at a one time safely.
		//Their shared socket communication, in other words, is threadsafe.
		virtual PvdDataStream&				createDataStream() = 0;

		//Create a channel to render immediate data on PVD.  Rendering is collected
		//per frame and then discared.  A line is only draw for the frame
		//in which it was received.
		virtual renderer::PvdUserRenderer&	createRenderer() = 0;

		//May actively change during debugging.
		//Getting this variable may block until the read thread 
		//is disconnected or releases the connection state mutex.
		virtual PvdConnectionState::Enum	getConnectionState() = 0;

		//gets the connection state which will block if the system is paused.
		//checks the connection for errors and disconnects if there are any.
		virtual void checkConnection() = 0;

		//Will currently never change during debugging
		virtual TConnectionFlagsType getConnectionType() = 0;

		virtual bool isConnected() = 0;
		virtual void disconnect() = 0;
		//flush profile and memory data.
		//This does not flush the socket for performance reasons.
		virtual void flush() = 0;

		//Connections *always* have an out stream, although they may not
		//have an *in* stream.
		virtual PvdNetworkOutStream& lockOutStream() = 0;
		virtual void unlockOutStream() = 0;
	};

}}


/** \brief Convenience typedef for the PvdConnection. */
typedef debugger::comm::PvdConnection PxVisualDebuggerConnection;
}
#endif

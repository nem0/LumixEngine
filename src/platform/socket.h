#pragma once


#include "core/lux.h"


#ifndef DISABLE_NETWORK

namespace Lux
{
	namespace Net
	{
		class LUX_PLATFORM_API Socket
		{
		public:
			Socket();
			~Socket();

			static bool init();

			// server interface
			bool create(const char* ip, uint16_t port);
			Socket* accept();

			// client interface
			Socket* connect(const char* ip, uint16_t port);

			// independent
			bool send(const void* data, int size);
			int receive(void* data, int size);
			bool receiveAllBytes(void* data, int size);
			bool canReceive();

		private:
			struct SocketImpl* m_implmentation;
		};
	} // ~namespace Net
} // ~namespace Lux

#endif // DISABLE_NETWORK
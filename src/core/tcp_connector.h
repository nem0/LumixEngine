#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace Net
	{
		class TCPStream;
		class Socket;

		class LUX_CORE_API TCPConnector
		{
		public:
			TCPConnector();
			~TCPConnector();

			TCPStream* connect(const char* ip, uint16_t port);

		private:
			Socket* m_socket;
		};
	} // ~namespace Net
} // ~namespace Lux
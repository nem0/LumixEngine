#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace Net
	{
		class TCPStream;
		class Socket;

		class LUX_CORE_API TCPAcceptor
		{
		public:
			TCPAcceptor();
			~TCPAcceptor();

			bool start(const char* ip, uint16_t port);
			TCPStream* accept();

		private:
			Socket* m_socket;
		};
	} // ~namespace Net
} // ~namespace Lux
#pragma once

#include "core/lumix.h"

namespace Lumix
{
	namespace Net
	{
		class TCPStream;

		class LUX_CORE_API TCPAcceptor
		{
		public:
			TCPAcceptor() {}
			~TCPAcceptor();

			bool start(const char* ip, uint16_t port);
			TCPStream* accept();
			
		private:
			uintptr_t m_socket;
		};
	} // ~namespace Net
} // ~namespace Lumix

#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace Net
	{
		class TCPStream;

		class LUX_CORE_API TCPConnector
		{
		public:
			TCPConnector() : m_socket(0) {}
			~TCPConnector();

			TCPStream* connect(const char* ip, uint16_t port);
		private:
			uintptr_t m_socket;
		};
	} // ~namespace Net
} // ~namespace Lux

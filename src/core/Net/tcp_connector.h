#pragma once

#include "core/lumix.h"

namespace Lumix
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
} // ~namespace Lumix

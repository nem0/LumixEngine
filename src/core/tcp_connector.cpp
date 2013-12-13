#include "core/tcp_connector.h"
#include "core/tcp_stream.h"

#include "platform/socket.h"

namespace Lux
{
	namespace Net
	{
		TCPConnector::TCPConnector()
		{
			m_socket = new Socket();
		}

		TCPConnector::~TCPConnector()
		{
			delete m_socket;
		}

		TCPStream* TCPConnector::connect(const char* ip, uint16_t port)
		{
			Socket* socket = m_socket->connect(ip, port);
			return new TCPStream(socket);
		}
	} // ~namespace Net
} // ~namespace Lux
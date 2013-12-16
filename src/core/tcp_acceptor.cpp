#include "core/tcp_acceptor.h"
#include "core/tcp_stream.h"

#include "platform/socket.h"

namespace Lux
{
	namespace Net
	{
		TCPAcceptor::TCPAcceptor()
		{
			m_socket = new Socket();
		}

		TCPAcceptor::~TCPAcceptor()
		{
			delete m_socket;
		}

		bool TCPAcceptor::start(const char* ip, uint16_t port)
		{
			return m_socket->create(ip, port);
		}

		TCPStream* TCPAcceptor::accept()
		{
			Socket* wrk = m_socket->accept();
			return new TCPStream(wrk);
		}
	} // ~namespace Net
} // ~namespace Lux
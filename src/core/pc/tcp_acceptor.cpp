#include "core/tcp_acceptor.h"
#include "core/tcp_stream.h"

#include <Windows.h>

namespace Lux
{
	namespace Net
	{
		TCPAcceptor::~TCPAcceptor()
		{
			::closesocket(m_socket);
		}

		bool TCPAcceptor::start(const char* ip, uint16_t port)
		{
			WORD sockVer;
			WSADATA wsaData;
			sockVer = MAKEWORD(2,2);
			if(WSAStartup(sockVer, &wsaData) != 0)
			{
				return false;
			}

			SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(socket == INVALID_SOCKET)
			{
				return false;
			}

			SOCKADDR_IN sin;
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = htons(port);
			sin.sin_addr.s_addr = ip ? ::inet_addr(ip) : INADDR_ANY;

			int retVal = ::bind(socket, (LPSOCKADDR)&sin, sizeof(sin));
			int err = ::WSAGetLastError();
			if(retVal == SOCKET_ERROR)
			{
				return false;
			}

			m_socket = socket;

			return ::listen(socket, 10) == 0;	
		}

		TCPStream* TCPAcceptor::accept()
		{
			SOCKET socket = ::accept(m_socket, NULL, NULL);
			return LUX_NEW(TCPStream)(socket);
		}
	} // ~namespace Net
} // ~namespace Lux
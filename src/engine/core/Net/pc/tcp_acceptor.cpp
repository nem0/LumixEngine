#include "core/net/tcp_acceptor.h"
#include "core/iallocator.h"
#include "core/net/tcp_stream.h"
#include <Windows.h>

namespace Lumix
{
	namespace Net
	{
		TCPAcceptor::~TCPAcceptor()
		{
			::closesocket(m_socket);
		}

		bool TCPAcceptor::start(const char* ip, uint16 port)
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
			if(retVal == SOCKET_ERROR)
			{
				return false;
			}

			m_socket = socket;

			return ::listen(socket, 10) == 0;	
		}

		void TCPAcceptor::close(TCPStream* stream)
		{
			m_allocator.deleteObject(stream);
		}

		TCPStream* TCPAcceptor::accept()
		{
			SOCKET socket = ::accept(m_socket, nullptr, nullptr);
			return m_allocator.newObject<TCPStream>(socket);
		}
	} // ~namespace Net
} // ~namespace Lumix

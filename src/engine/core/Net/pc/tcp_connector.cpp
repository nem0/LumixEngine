#include "core/net/tcp_connector.h"
#include "core/iallocator.h"
#include "core/net/tcp_stream.h"

#ifndef DISABLE_NETWORK

#include "core/pc/simple_win.h"
#undef WIN32_LEAN_AND_MEAN
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib")

namespace Lumix
{
	namespace Net
	{
		TCPConnector::~TCPConnector()
		{
			::closesocket(m_socket);
		}

		TCPStream* TCPConnector::connect(const char* ip, uint16 port)
		{
			WORD sockVer;
			WSADATA wsaData;
			sockVer = MAKEWORD(2,2);
			if(WSAStartup(sockVer, &wsaData) != 0)
			{
				return nullptr;
			}

			SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(socket == INVALID_SOCKET)
			{
				return nullptr;
			}

			SOCKADDR_IN sin;

			memset (&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = htons(port);
			sin.sin_addr.s_addr = ip ? ::inet_addr(ip) : INADDR_ANY; 

			if (::connect(socket, (LPSOCKADDR)&sin, sizeof(sin)) != 0) 
			{
				return nullptr;
			}

			m_socket = socket;
			return LUMIX_NEW(m_allocator, TCPStream)(socket);
		}

		void TCPConnector::close(TCPStream* stream)
		{
			LUMIX_DELETE(m_allocator, stream);
		}
	} // ~namespace Net
} // ~namespace Lumix

#endif DISABLE_NETWORK

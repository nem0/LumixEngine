#include "platform/tcp_connector.h"
#include "platform/tcp_stream.h"

#ifndef DISABLE_NETWORK

#include <Windows.h>

#pragma comment(lib,"Ws2_32.lib")

namespace Lux
{
	namespace Net
	{
		TCPConnector::~TCPConnector()
		{
			::closesocket(m_socket);
		}

		TCPStream* TCPConnector::connect(const char* ip, uint16_t port)
		{
			WORD sockVer;
			WSADATA wsaData;
			sockVer = MAKEWORD(2,2);
			if(WSAStartup(sockVer, &wsaData) != 0)
			{
				return NULL;
			}

			SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(socket == INVALID_SOCKET)
			{
				return NULL;
			}

			SOCKADDR_IN sin;

			memset (&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = htons(port);
			sin.sin_addr.s_addr = ip ? ::inet_addr(ip) : INADDR_ANY; 

			if (::connect(socket, (LPSOCKADDR)&sin, sizeof(sin)) != 0) 
			{
				return NULL;
			}

			return new TCPStream(socket);		
		}
	} // ~namespace Net
} // ~namespace Lux

#endif DISABLE_NETWORK

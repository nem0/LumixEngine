#include "core/tcp_stream.h"

#include <string.h>
#include <Windows.h>

namespace Lux
{
	namespace Net
	{
		TCPStream::~TCPStream()
		{
			::closesocket(m_socket);
		}

		bool TCPStream::read(char* string, uint32_t max_size)
		{
			uint32_t len = 0;
			bool ret = true;
			ret &= read(len);
			ASSERT(len < max_size);
			ret &= read((void*)string, len);

			return ret;
		}

		bool TCPStream::write(const char* string)
		{
			uint32_t len = strlen(string) + 1;
			bool ret = write(len);
			ret &= write((const void*)string, len);

			return ret;
		}

		bool TCPStream::read(void* buffer, size_t size)
		{
			int to_receive = size;
			char* ptr = static_cast<char*>(buffer);

			do
			{
				int received = ::recv(m_socket, ptr, to_receive, 0);
				ptr += received;
				to_receive -= received;
				if(received <= 0)
				{
					int err = WSAGetLastError();
					if(err == WSAEWOULDBLOCK)
					{
						ptr -= received;
						to_receive += received;
					}
					else
					{
						return false;
					}
				}
			} while(to_receive > 0);
			return true;
		}

		bool TCPStream::write(const void* buffer, size_t size)
		{
			int send = ::send(m_socket, static_cast<const char*>(buffer), size, 0);

			//todo: handle errors

			return send == size;
		}
	} // ~namespace Net
} // ~namespace Lux
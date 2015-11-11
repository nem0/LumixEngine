#include "core/net/tcp_stream.h"

#include <Windows.h>


namespace Lumix
{
	namespace Net
	{
		TCPStream::~TCPStream()
		{
			::closesocket(m_socket);
		}

		bool TCPStream::readString(char* string, uint32 max_size)
		{
			uint32 len = 0;
			bool ret = true;
			ret &= read(len);
			ASSERT(len < max_size);
			ret &= read((void*)string, len);

			return ret;
		}

		bool TCPStream::writeString(const char* string)
		{
			uint32 len = (uint32)strlen(string) + 1;
			bool ret = write(len);
			ret &= write((const void*)string, len);

			return ret;
		}

		bool TCPStream::read(void* buffer, size_t size)
		{
			int32 to_receive = (int32)size;
			char* ptr = static_cast<char*>(buffer);

			do
			{
				int received = ::recv(m_socket, ptr, to_receive, 0);
				ptr += received;
				to_receive -= received;
				if(received <= 0)
				{
					if(WSAGetLastError() == WSAEWOULDBLOCK)
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
			int send = ::send(m_socket, static_cast<const char*>(buffer), (int)size, 0);
			return (size_t)send == size;
		}
	} // ~namespace Net
} // ~namespace Lumix

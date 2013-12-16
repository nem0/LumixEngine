#include "core/tcp_stream.h"

#include "platform/socket.h"
#include <string.h>

namespace Lux
{
	namespace Net
	{
			bool TCPStream::read(uint8_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(int8_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(uint16_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(int16_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(uint32_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(int32_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(uint64_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(int64_t& val)
			{
				return m_socket->receiveAllBytes(&val, sizeof(val));
			}

			bool TCPStream::read(char* string, uint32_t max_size)
			{
				uint32_t len = 0;
				bool ret = true;
				ret &= read(len);
				ASSERT(len < max_size);
				ret &= m_socket->receiveAllBytes(string, len);

				return ret;
			}

			bool TCPStream::write(uint8_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(int8_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(uint16_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(int16_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(uint32_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(int32_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(uint64_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(int64_t val)
			{
				return m_socket->send(&val, sizeof(val));
			}

			bool TCPStream::write(const char* string)
			{
				uint32_t len = strlen(string) + 1;
				bool ret = write(len);
				ret &= m_socket->send(string, len);
				
				return ret;
			}

			bool TCPStream::read(void* buffer, size_t size)
			{
				return m_socket->receiveAllBytes(buffer, size);
			}

			bool TCPStream::write(const void* buffer, size_t size)
			{
				return m_socket->send(buffer, size);
			}
	} // ~namespace Net
} // ~namespace Lux
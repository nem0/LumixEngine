#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace Net
	{
		class Socket;

		class LUX_CORE_API TCPStream
		{
		public:
			TCPStream(Socket* socket) : m_socket(socket) { } 
			~TCPStream() {}

			bool read(uint8_t& val);
			bool read(int8_t& val);
			bool read(uint16_t& val);
			bool read(int16_t& val);
			bool read(uint32_t& val);
			bool read(int32_t& val);
			bool read(uint64_t& val);
			bool read(int64_t& val);
			bool read(char* string, uint32_t max_size);

			bool write(uint8_t val);
			bool write(int8_t val);
			bool write(uint16_t val);
			bool write(int16_t val);
			bool write(uint32_t val);
			bool write(int32_t val);
			bool write(uint64_t val);
			bool write(int64_t val);
			bool write(const char* string);

			bool read(void* buffer, size_t size);
			bool write(const void* buffer, size_t size);

		private:
			TCPStream();

			Socket* m_socket;
		};
	} // ~namespace Net
} // ~namespace Lux
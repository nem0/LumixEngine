#pragma once

#include "core/lux.h"
#include <string.h>

namespace Lux
{
	namespace Net
	{
		class LUX_CORE_API TCPStream
		{
		public:
			TCPStream(uintptr_t socket) : m_socket(socket) { } 
			~TCPStream();

			LUX_FORCE_INLINE bool read(uint8_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(int8_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(uint16_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(int16_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(uint32_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(int32_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(uint64_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool read(int64_t& val)
			{
				return read(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(uint8_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(int8_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(uint16_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(int16_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(uint32_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(int32_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(uint64_t val)
			{
				return write(&val, sizeof(val));
			}

			LUX_FORCE_INLINE bool write(int64_t val)
			{
				return write(&val, sizeof(val));
			}

			bool read(char* string, uint32_t max_size);
			bool write(const char* string);

			bool read(void* buffer, size_t size);
			bool write(const void* buffer, size_t size);

		private:
			TCPStream();

			uintptr_t m_socket;
		};
	} // ~namespace Net
} // ~namespace Lux
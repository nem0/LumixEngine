#pragma once

#include "lumix.h"


namespace Lumix
{
	namespace Net
	{
		class LUMIX_ENGINE_API TCPStream
		{
		public:
			TCPStream(uintptr socket) : m_socket(socket) { } 
			~TCPStream();

			LUMIX_FORCE_INLINE bool read(bool& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(uint8& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(int8& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(uint16& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(int16& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(uint32& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(int32& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(uint64& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool read(int64& val)
			{
				return read(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(bool val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(uint8 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(int8 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(uint16 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(int16 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(uint32 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(int32 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(uint64 val)
			{
				return write(&val, sizeof(val));
			}

			LUMIX_FORCE_INLINE bool write(int64 val)
			{
				return write(&val, sizeof(val));
			}

			bool readString(char* string, uint32 max_size);
			bool writeString(const char* string);

			bool read(void* buffer, size_t size);
			bool write(const void* buffer, size_t size);

		private:
			TCPStream();

			uintptr m_socket;
		};
	} // ~namespace Net
} // ~namespace Lumix

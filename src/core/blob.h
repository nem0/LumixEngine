#pragma once


#include "core/lumix.h"
#include "core/array.h"


namespace Lumix
{

	class LUMIX_CORE_API Blob
	{
		public:
			explicit Blob(IAllocator& allocator);
			Blob(const Blob& rhs, IAllocator& allocator);
			void operator =(const Blob& rhs);

			void reserve(int size) { m_buffer.reserve(size); }
			void create(const void* data, int size) { m_data = data; m_size = size; m_pos = 0; }
			void write(const void* data, int32_t size);
			bool read(void* data, int32_t size);
			const uint8_t* getBuffer() const { return &m_buffer[0]; }
			const uint8_t* getData() const { return static_cast<const uint8_t*>(m_data); }
			int getBufferSize() const { return m_size; }
			void flush() { m_size = 0; }
			void clearBuffer() { m_buffer.clear(); m_pos = 0; m_size = 0; }

			template <class T>
			void write(T value) { write(&value, sizeof(T)); }
			void writeString(const char* string);
			void readString(char* out, int max_size);
			
			template <class T>
			void read(T& value) { read(&value, sizeof(T)); }

			void rewindForRead();

		private:
			Blob(const Blob& rhs);
			void write(const char*);
			void read(const char*);

		private:
			IAllocator& m_allocator;
			Array<uint8_t> m_buffer;
			int m_pos;
			int m_size;
			const void* m_data; 
	};

} // !namespace Lumix

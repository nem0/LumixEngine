#pragma once


#include "core/lux.h"
#include "core/array.h"


namespace Lux
{

	class LUX_CORE_API Blob
	{
		public:
			Blob();

			void create(const void* data, int size) { m_data = data; m_size = size; m_pos = 0; }
			virtual void write(const void* data, int32_t size);
			virtual bool read(void* data, int32_t size);
			const uint8_t* getBuffer() const { return &m_buffer[0]; }
			int getBufferSize() const { return m_size; }
			void flush() { m_size = 0; }
			void clearBuffer() { m_buffer.clear(); m_pos = 0; m_size = 0; }

			template <class T>
			void write(T value) { write(&value, sizeof(T)); }
			void write(const char* string);

			template <class T>
			void read(T& value) { read(&value, sizeof(T)); }

			void rewindForRead() { m_pos = 0; m_data = &m_buffer[0]; m_size = m_buffer.size(); }


		private:
			Array<uint8_t> m_buffer;
			int m_pos;
			int m_size;
			const void* m_data; 
	};

} // !namespace Lux
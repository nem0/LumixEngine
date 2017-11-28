#pragma once


#include "engine/lumix.h"
#include "engine/array.h"


namespace Lumix
{

	class InputBlob;


	class LUMIX_ENGINE_API OutputBlob
	{
		public:
			explicit OutputBlob(IAllocator& allocator);
			OutputBlob(void* data, int size);
			OutputBlob(const OutputBlob& rhs);
			OutputBlob(const OutputBlob& blob, IAllocator& allocator);
			OutputBlob(const InputBlob& blob, IAllocator& allocator);
			void operator =(const OutputBlob& rhs);
			~OutputBlob();

			void resize(int size);
			void reserve(int size);
			const void* getData() const { return m_data; }
			void* getMutableData() { return m_data; }
			int getPos() const { return m_pos; }
			void write(const string& string);
			void write(const void* data, int size);
			void writeString(const char* string);
			template <class T> void write(const T& value);
			void clear();

			OutputBlob& operator << (const char* str);
			OutputBlob& operator << (u64 value);
			OutputBlob& operator << (i64 value);
			OutputBlob& operator << (i32 value);
			OutputBlob& operator << (u32 value);
			OutputBlob& operator << (float value);

		private:
			void* m_data;
			int m_size;
			int m_pos;
			IAllocator* m_allocator;
	};

	template <class T> void OutputBlob::write(const T& value)
	{
		write(&value, sizeof(T));
	}

	template <> inline void OutputBlob::write<bool>(const bool& value)
	{
		u8 v = value;
		write(&v, sizeof(v));
	}

	class LUMIX_ENGINE_API InputBlob
	{
		public:
			InputBlob(const void* data, int size);
			explicit InputBlob(const OutputBlob& blob);

			bool read(string& string);
			bool read(void* data, int size);
			bool readString(char* data, int max_size);
			template <class T> void read(T& value) { read(&value, sizeof(T)); }
			template <class T> T read();
			const void* skip(int size);
			const void* getData() const { return (const void*)m_data; }
			int getSize() const { return m_size; }
			int getPosition() { return m_pos; }
			void setPosition(int pos) { m_pos = pos; }
			void rewind() { m_pos = 0; }
			u8 readChar() { ++m_pos; return m_data[m_pos - 1]; }

		private:
			const u8* m_data;
			int m_size;
			int m_pos;
	};

	template <class T> T InputBlob::read()
	{
		T v;
		read(&v, sizeof(v));
		return v;
	}

	template <> inline bool InputBlob::read<bool>()
	{
		u8 v;
		read(&v, sizeof(v));
		return v != 0;
	}
} // !namespace Lumix

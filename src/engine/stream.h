#pragma once

#include "lumix.h"


namespace Lumix
{


struct IAllocator;
class String;


struct LUMIX_ENGINE_API IOutputStream
{
	virtual bool write(const void* buffer, u64 size) = 0;

	IOutputStream& operator << (const char* str);
	IOutputStream& operator << (u64 value);
	IOutputStream& operator << (i64 value);
	IOutputStream& operator << (i32 value);
	IOutputStream& operator << (u32 value);
	IOutputStream& operator << (float value);
	IOutputStream& operator << (double value);
	template <class T> void write(const T& value);
	void writeString(const char* string);
};


struct LUMIX_ENGINE_API IInputStream
{
	virtual bool read(void* buffer, u64 size) = 0;
	virtual const void* getBuffer() const = 0;
	virtual u64 size() const = 0;
	
	template <class T> void read(T& value) { read(&value, sizeof(T)); }
	template <class T> T read();
	bool readString(const Span<char>& data);
};


class InputMemoryStream;


class LUMIX_ENGINE_API OutputMemoryStream final : public IOutputStream
{
	public:
		explicit OutputMemoryStream(IAllocator& allocator);
		OutputMemoryStream(void* data, u64 size);
		OutputMemoryStream(OutputMemoryStream&& rhs);
		OutputMemoryStream(const OutputMemoryStream& rhs);
		OutputMemoryStream(const OutputMemoryStream& blob, IAllocator& allocator);
		OutputMemoryStream(const InputMemoryStream& blob, IAllocator& allocator);
		void operator =(const OutputMemoryStream& rhs);
		~OutputMemoryStream();

		bool write(const void* data, u64 size) override;

		Span<u8> releaseOwnership();
		void resize(u64 size);
		void reserve(u64 size);
		const u8* getData() const { return m_data; }
		u8* getMutableData() { return m_data; }
		u64 getPos() const { return m_pos; }
		void write(const String& string);
		template <class T> void write(const T& value);
		void clear();
		void* skip(int size);
		bool empty() const { return m_pos == 0; }

	private:
		u8* m_data;
		u64 m_size;
		u64 m_pos;
		IAllocator* m_allocator;
};


template <class T> void OutputMemoryStream::write(const T& value)
{
	write(&value, sizeof(T));
}

template <> inline void OutputMemoryStream::write<bool>(const bool& value)
{
	u8 v = value;
	write(&v, sizeof(v));
}


class LUMIX_ENGINE_API InputMemoryStream final : public IInputStream
{
	public:
		InputMemoryStream(const void* data, u64 size);
		explicit InputMemoryStream(const OutputMemoryStream& blob);

		void set(const void* data, u64 size);
		bool read(void* data, u64 size) override;
		bool read(String& string);
		const void* skip(u64 size);
		const void* getData() const { return (const void*)m_data; }
		const void* getBuffer() const override { return m_data; }
		u64 size() const override { return m_size; }
		u64 getPosition() const { return m_pos; }
		void setPosition(u64 pos) { m_pos = pos; }
		void rewind() { m_pos = 0; }
		u8 readChar() { ++m_pos; return m_data[m_pos - 1]; }

        using IInputStream::read;
	private:
		const u8* m_data;
		u64 m_size;
		u64 m_pos;
};

template <class T> T IInputStream::read()
{
	T v;
	read(&v, sizeof(v));
	return v;
}

template <> inline bool IInputStream::read<bool>()
{
	u8 v;
	read(&v, sizeof(v));
	return v != 0;
}


template <class T> void IOutputStream::write(const T& value)
{
	write(&value, sizeof(T));
}


} // namespace Lumix
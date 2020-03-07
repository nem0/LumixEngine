#pragma once

#include "lumix.h"


namespace Lumix
{


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
	template <typename T> void write(const T& value);
};


struct LUMIX_ENGINE_API IInputStream
{
	virtual bool read(void* buffer, u64 size) = 0;
	virtual const void* getBuffer() const = 0;
	virtual u64 size() const = 0;
	
	template <typename T> void read(T& value) { read(&value, sizeof(T)); }
	template <typename T> T read();
	template <typename T> void read(Ref<T> val) { val = read<T>(); }
};


struct LUMIX_ENGINE_API OutputMemoryStream final : IOutputStream
{
public:
	explicit OutputMemoryStream(struct IAllocator& allocator);
	OutputMemoryStream(void* data, u64 size);
	OutputMemoryStream(OutputMemoryStream&& rhs);
	OutputMemoryStream(const OutputMemoryStream& rhs);
	OutputMemoryStream(const OutputMemoryStream& blob, IAllocator& allocator);
	OutputMemoryStream(const struct InputMemoryStream& blob, IAllocator& allocator);
	~OutputMemoryStream();
	void operator =(const OutputMemoryStream& rhs);
	void operator =(OutputMemoryStream&& rhs);

	bool write(const void* data, u64 size) override;

	Span<u8> releaseOwnership();
	void resize(u64 size);
	void reserve(u64 size);
	const u8* getData() const { return m_data; }
	u8* getMutableData() { return m_data; }
	u64 getPos() const { return m_pos; }
	void write(const struct String& string);
	void writeString(const char* string);
	template <typename T> void write(const T& value);
	void clear();
	void* skip(int size);
	bool empty() const { return m_pos == 0; }

private:
	u8* m_data;
	u64 m_size;
	u64 m_pos;
	IAllocator* m_allocator;
};


template <typename T> void OutputMemoryStream::write(const T& value)
{
	write(&value, sizeof(T));
}

template <> inline void OutputMemoryStream::write<bool>(const bool& value)
{
	u8 v = value;
	write(&v, sizeof(v));
}


struct LUMIX_ENGINE_API InputMemoryStream final : IInputStream
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
	const char* readString();

	using IInputStream::read;
private:
	const u8* m_data;
	u64 m_size;
	u64 m_pos;
};

template <typename T> T IInputStream::read()
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


template <typename T> void IOutputStream::write(const T& value)
{
	write(&value, sizeof(T));
}


} // namespace Lumix
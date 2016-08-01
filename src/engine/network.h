#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class IAllocator;


namespace Net
{


class TCPStream;


class LUMIX_ENGINE_API TCPAcceptor
{
public:
	TCPAcceptor(IAllocator& allocator);
	~TCPAcceptor();

	bool start(const char* ip, uint16 port);
	TCPStream* accept();
	void close(TCPStream* stream);

private:
	IAllocator& m_allocator;
	uintptr m_socket;
};


class LUMIX_ENGINE_API TCPConnector
{
public:
	TCPConnector(IAllocator& allocator);
	~TCPConnector();

	TCPStream* connect(const char* ip, uint16 port);
	void close(TCPStream* stream);

private:
	IAllocator& m_allocator;
	uintptr m_socket;
};


class LUMIX_ENGINE_API TCPStream
{
public:
	TCPStream(uintptr socket)
		: m_socket(socket)
	{
	}
	~TCPStream();

	template <typename T> LUMIX_FORCE_INLINE bool read(T& val) { return read(&val, sizeof(val)); }
	template <typename T> LUMIX_FORCE_INLINE bool write(T val) { return write(&val, sizeof(val)); }

	bool readString(char* string, uint32 max_size);
	bool writeString(const char* string);

	bool read(void* buffer, size_t size);
	bool write(const void* buffer, size_t size);

private:
	TCPStream();

	uintptr m_socket;
};


} // namespace Net
} // namespace Lumix

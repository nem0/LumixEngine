#include "engine/core/lux.h"
#include "socket.h"


#ifndef DISABLE_NETWORK



struct SocketImpl
{
};


Socket::Socket()
{
	m_implmentation = new SocketImpl;
}


Socket::~Socket()
{
	delete m_implmentation;
}


bool Socket::init()
{
	return false;
}

bool Socket::create(unsigned short port)
{
	return false;
}

Socket* Socket::accept()
{
	return LUX_NULL;;
}

bool Socket::send(const void* data, int size)
{
	return false;
}

int Socket::receive(void* data, int size)
{
	return false;
}

bool Socket::canReceive()
{
	return false;
}

bool Socket::receiveAllBytes(void* data, int size)
{
	return false;
}


#endif // DISABLE_NETWORK

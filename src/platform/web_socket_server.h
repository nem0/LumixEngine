#pragma once


#ifndef DISABLE_NETWORK


#include <string>
#include "core/lux.h"


namespace Lux
{


class LUX_PLATFORM_API WebSocketServer
{
	friend class WebSocketServerTask;
	public:
		typedef void (*Callback)(void*, unsigned char*, int);

	public:
		WebSocketServer();
		~WebSocketServer();

		bool create();
		bool destroy();
		void send(const char* msg, int size);
		void setCallback(Callback callback, void* data) { m_callback = callback; m_callback_data = data; }

	private:
		Callback m_callback;
		void* m_callback_data;
		struct WebSocketImpl* m_implementation;
};


} // !namespace Lux


#endif // DISABLE_NETWORK

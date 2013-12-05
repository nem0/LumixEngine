#include "web_socket_server.h"
#include "socket.h"
#include "task.h"
#include <cstring>
#include "core/sha1.h"
#include "core/base64.h"


#ifndef DISABLE_NETWORK


namespace Lux
{


static const char* magick_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
static const int BUFFER_SIZE = 1024* 1024;

class WebSocketServerTask : public Task
{
	friend class WebSocketServer;
	public:
		WebSocketServerTask(WebSocketServer& server)
			: m_server(server)
		{
			m_socket = 0;
			m_buffer = new unsigned char[BUFFER_SIZE];
		}

		~WebSocketServerTask()
		{
			delete[] m_buffer;
		}

		virtual int task()
		{
			return 0;
		}

		void send(const char* msg, int size)
		{
			
		}

	private:
		int encodeFrame(const unsigned char* msg, int size, unsigned char* frame)
		{
			return 0;
		}

		int decodeFrame(const unsigned char* frame, int size, unsigned char* out)
		{
			return 0;
		}

		void getKey(const char* line, char* key)
		{
		}

		void computeResponse(char* key)
		{
		}

		void receiveHandshake(Socket& s)
		{
		}

	private:
		Socket*				m_socket;
		WebSocketServer&	m_server;
		unsigned char*		m_buffer;
};


struct WebSocketImpl
{
	WebSocketImpl(WebSocketServer& server) 
		: task(server)
	{
	}
	WebSocketServerTask task;
};


WebSocketServer::WebSocketServer()
{
	m_implementation = new WebSocketImpl(*this);
}


WebSocketServer::~WebSocketServer()
{
	delete m_implementation;
}


bool WebSocketServer::create()
{
	bool ret = m_implementation->task.create();
	ret = ret && m_implementation->task.run();
	return ret;
}


bool WebSocketServer::destroy()
{
	return true;
}


void WebSocketServer::send(const char* msg, int size)
{
	m_implementation->task.send(msg, size);
}


} // !namespace Lux


#endif // DISABLE_NETWORK

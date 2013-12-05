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
			if(!Socket::init())
				return -1;
			Socket s;
			if(!s.create(10081))
				return -1;
			while(true)
			{
				m_socket = s.accept();

				unsigned char frame[255];
				receiveHandshake(*m_socket);
				int received;
				do
				{
					received = m_socket->receive(frame, 254);
					unsigned char tmp[255];
					int msg_size = decodeFrame(frame, received, tmp);
					if(m_server.m_callback)
					{
						(*m_server.m_callback)(m_server.m_callback_data, tmp, msg_size);
					}
				} while( received > 0 );
				delete m_socket;
				m_socket = 0;
			}
			return 0;
		}

		void send(const char* msg, int size)
		{
			if(m_socket)
			{
				int msg_size = encodeFrame((const unsigned char*)msg, size, m_buffer);
				m_socket->send(m_buffer, msg_size);
			}
		}

	private:
		int encodeFrame(const unsigned char* msg, int size, unsigned char* frame)
		{
			int data_start = 2;
			frame[0] = 0x81;
			if(size > 125)
			{
				data_start = 4;
				frame[1] = 126;
				short tmp = (short)size;
				frame[2] = (unsigned char)(tmp >> 8);
				frame[3] = (unsigned char)(tmp & 0xff);
			}
			else 
			{
				frame[1] = size & 0x7f;
			}
			for(int i = 0; i < size; ++i)
			{
				frame[data_start + i] = msg[i];
			}
			return size + data_start;
		}

		int decodeFrame(const unsigned char* frame, int size, unsigned char* out)
		{
			const unsigned char* mask = frame + 2;
			const unsigned char* from = frame + 6;
			unsigned char* to = out;
			for(int i = 0; i < size - 6; ++i)
			{
				*to = *(from + i) ^ mask[i % 4];
				++to;
			}
			*to = 0;
			return size - 6;
		}

		void getKey(const char* line, char* key)
		{
			const char* found = strstr(line, "Sec-WebSocket-Key: ");
			if(found)
			{
				found += strlen("Sec-WebSocket-Key: ");
				char* to_key = key;
				while(*found != '\r' && *found != '\n')
				{
					*key = *found;
					++key;
					++found;
				}
				*key = 0;
			}
		}

		void computeResponse(char* key)
		{
			strcat(key, magick_string);
			unsigned char hash[96];
			sha1(key, strlen(key), hash);
			char hashHex[96];
			sha1toHexString(hash, hashHex);
			base64Encode(hash, 20, (unsigned char*)key);
			//base64Encode((unsigned char*)hashHex, strlen(hashHex), (unsigned char*)key);
		}

		void receiveHandshake(Socket& s)
		{
			char key[255];
			int received = s.receive(m_buffer, 2048);
			m_buffer[received] = 0;
			getKey((char*)m_buffer, key);
			//strcpy(key, "x3JJHMbDL1EzLkh9GBhXDw==");
			computeResponse(key);
			const char* str = "HTTP/1.1 101 Web Socket Protocol Handshake\r\n\
Upgrade: WebSocket\r\n\
Connection: Upgrade\r\n\
Sec-WebSocket-Accept: %s\r\n\r\n";
			sprintf((char*)m_buffer, str, key);
			s.send(m_buffer, strlen((char*)m_buffer));
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

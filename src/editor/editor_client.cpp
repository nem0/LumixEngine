#include "editor_client.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/pod_array.h"
#include "editor/client_message_types.h"
#include "editor/server_message_types.h"
#include "core/task.h"
#include "core/tcp_connector.h"
#include "core/tcp_stream.h"

namespace Lux
{

	class ReceiveTask : public MT::Task
	{
		public:	
			virtual int task() LUX_OVERRIDE;
			
			struct EditorClientImpl* m_client;
	};


	struct EditorClientImpl
	{
		void sendMessage(uint32_t type, const void* data, int32_t size);
		void onMessage(uint8_t* data, int size);

		Net::TCPConnector m_connector;
		Net::TCPStream* m_stream;
		ReceiveTask m_task;
		EventManager m_event_manager;
	};


	int ReceiveTask::task()
	{
		bool finished = false;
		PODArray<uint8_t> data;
		data.resize(8);
		while(!finished)
		{
			if(m_client->m_stream->read(&data[0], 8))
			{
				int length = *(int*)&data[0];
				int guard = *(int*)&data[4];
				ASSERT(guard == 0x12345678);
				if(length > 0)
				{
					data.resize(length);
					m_client->m_stream->read(&data[0], length);
					m_client->onMessage(&data[0], data.size());
				}
			}
		}
		return 1;
	}


	bool EditorClient::create()
	{
		m_impl = LUX_NEW(EditorClientImpl);
		m_impl->m_stream = m_impl->m_connector.connect("127.0.0.1", 10008);
		m_impl->m_task.m_client = m_impl;
		bool success = m_impl->m_task.create("ClientReceiver");
		success = success && m_impl->m_task.run();
		return success && m_impl->m_stream != NULL;
	}


	void EditorClientImpl::onMessage(uint8_t* data, int size)
	{
		Blob stream;
		stream.create(data, size);
		int32_t message_type;
		stream.read(message_type);
		switch(message_type)
		{
			case ServerMessageType::ENTITY_POSITION:
				{
					EntityPositionEvent msg;
					msg.read(stream);
					m_event_manager.emitEvent(msg);
				}
				break;
			case ServerMessageType::ENTITY_SELECTED:
				{
					EntitySelectedEvent msg;
					msg.read(stream);
					m_event_manager.emitEvent(msg);
				}
				break;
			case ServerMessageType::PROPERTY_LIST:
				{
					PropertyListEvent msg;
					msg.read(stream);
					m_event_manager.emitEvent(msg);
				}
				break;
			case ServerMessageType::LOG_MESSAGE:
				{
					LogEvent msg;
					msg.read(stream);
					m_event_manager.emitEvent(msg);
				}
				break;
			default:
				break;
		}
	}


	void EditorClientImpl::sendMessage(uint32_t type, const void* data, int32_t size)
	{
		static const uint8_t header_end = 0;
		int32_t whole_size = size + 4;
		m_stream->write(&whole_size, sizeof(whole_size));
		m_stream->write(&header_end, sizeof(header_end));
		m_stream->write(&type, sizeof(type));
		if(data)
		{
			m_stream->write(data, size);
		}
	}


	void EditorClient::addComponent(uint32_t type)
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::ADD_COMPONENT, &type, sizeof(type));
	}

	void EditorClient::toggleGameMode()
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::TOGGLE_GAME_MODE, NULL, 0);
	}

	void EditorClient::addEntity()
	{
		m_impl->sendMessage((uint32_t)ClientMessageType::ADD_ENTITY, NULL, 0);
	}


	void EditorClient::mouseDown(int x, int y, int button)
	{
		int data[3] = {x, y, button};
		m_impl->sendMessage(ClientMessageType::POINTER_DOWN, data, 12);
	}


	void EditorClient::mouseUp(int x, int y, int button)
	{
		int data[3] = {x, y, button};
		m_impl->sendMessage(ClientMessageType::POINTER_UP, data, 12);
	}


	void EditorClient::mouseMove(int x, int y, int dx, int dy)
	{
		int data[4] = {x, y, dx, dy};
		m_impl->sendMessage(ClientMessageType::POINTER_MOVE, data, 16);

	}


	EventManager& EditorClient::getEventManager()
	{
		return m_impl->m_event_manager;
	}


	void EditorClient::loadUniverse(const char* path)
	{
		m_impl->sendMessage(ClientMessageType::LOAD, path, strlen(path)+1);
	}

	void EditorClient::saveUniverse(const char* path)
	{
		m_impl->sendMessage(ClientMessageType::SAVE, path, strlen(path)+1);
	}

	void EditorClient::navigate(float forward, float right, int32_t fast)
	{
		uint8_t data[12];
		*(float*)data = forward;
		*(float*)(data+4) = right;
		*(int32_t*)(data+8) = fast;
		m_impl->sendMessage(ClientMessageType::MOVE_CAMERA, data, 12);
	}


	void EditorClient::setComponentProperty(const char* component, const char* property, const void* value, int32_t length)
	{
		static Blob stream;
		stream.clearBuffer();
		uint32_t tmp = crc32(component);
		stream.write(&tmp, sizeof(tmp));
		tmp = crc32(property);
		stream.write(&tmp, sizeof(tmp));
		stream.write(&length, sizeof(length));
		stream.write(value, length);
		m_impl->sendMessage(ClientMessageType::PROPERTY_SET, stream.getBuffer(), stream.getBufferSize());
	}


	void EditorClient::requestProperties(uint32_t type_crc)
	{
		m_impl->sendMessage(ClientMessageType::GET_PROPERTIES, &type_crc, sizeof(type_crc));
	}


} // ~namespace Lux

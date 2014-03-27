#include "editor_client.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/mutex.h"
#include "core/task.h"
#include "core/tcp_connector.h"
#include "core/tcp_stream.h"
#include "editor/client_message_types.h"
#include "editor/server_message_types.h"
#include "universe/universe.h"

namespace Lux
{

	class ReceiveTask : public MT::Task
	{
	public:
		ReceiveTask() : m_finished(false) {}

		virtual int task() LUX_OVERRIDE;
		void stop() { m_finished = true; }

		struct EditorClientImpl* m_client;
	private:
		volatile bool m_finished;
	};


	struct EditorClientImpl
	{
		EditorClientImpl()
			: m_mutex(false)
		{
		}

		void sendMessage(uint32_t type, const void* data, int32_t size);
		void onMessage(uint8_t* data, int size);

		Net::TCPConnector m_connector;
		Net::TCPStream* m_stream;
		ReceiveTask m_task;
		EventManager m_event_manager;
		MT::Mutex m_mutex;
		Array<uint8_t*> m_messages;
		string m_base_path;
	};


	int ReceiveTask::task()
	{
		Array<uint8_t> data;
		data.resize(8);
		while(!m_finished)
		{
			if(m_client->m_stream->read(&data[0], 8))
			{
				int32_t length = *(int32_t*)&data[0];
				int32_t guard = *(int32_t*)&data[4];
				ASSERT(guard == 0x12345678);
				if(length > 0)
				{
					data.resize(length);
					/// TODO "stack" allocator
					uint8_t* msg = LUX_NEW_ARRAY(uint8_t, length + 4);
					m_client->m_stream->read(msg + 4, length);
					((int32_t*)msg)[0] = length;
					MT::Lock lock(m_client->m_mutex);
					m_client->m_messages.push(msg);
				}
			}
		}
		return 1;
	}


	void EditorClient::processMessages()
	{
		MT::Lock lock(m_impl->m_mutex);
		for (int i = 0; i < m_impl->m_messages.size(); ++i)
		{
			m_impl->onMessage(m_impl->m_messages[i] + 4, *(int32_t*)m_impl->m_messages[i]);
			LUX_DELETE_ARRAY(m_impl->m_messages[i]);
		}
		m_impl->m_messages.clear();
	}


	bool EditorClient::create(const char* base_path)
	{
		m_impl = LUX_NEW(EditorClientImpl);
		m_impl->m_base_path = base_path;
		m_impl->m_stream = m_impl->m_connector.connect("127.0.0.1", 10013);
		m_impl->m_task.m_client = m_impl;
		bool success = m_impl->m_task.create("ClientReceiver");
		success = success && m_impl->m_task.run();
		return success && m_impl->m_stream != NULL;
	}

	void EditorClient::destroy()
	{
		LUX_DELETE(m_impl->m_stream);
		m_impl->m_task.stop();
		m_impl->m_task.destroy();
		LUX_DELETE(m_impl);
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


	const char* EditorClient::getBasePath() const
	{
		return m_impl->m_base_path.c_str();
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
		m_impl->sendMessage(ClientMessageType::LOAD, path, (int32_t)strlen(path)+1);
	}

	void EditorClient::setEntityPosition(int32_t entity, const Vec3& position)
	{
		uint8_t data[sizeof(entity) + sizeof(position)];
		*(int32_t*)data = entity;
		*(Vec3*)(data + sizeof(entity)) = position;
		m_impl->sendMessage(ClientMessageType::SET_POSITION, data, sizeof(entity) + sizeof(position));
	}

	void EditorClient::saveUniverse(const char* path)
	{
		m_impl->sendMessage(ClientMessageType::SAVE, path, (int32_t)strlen(path)+1);
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

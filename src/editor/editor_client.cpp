#include "editor_client.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/fifo_allocator.h"
#include "core/MT/lock_free_queue.h"
#include "core/MT/mutex.h"
#include "core/MT/task.h"
#include "core/net/tcp_connector.h"
#include "core/net/tcp_stream.h"
#include "editor/client_message_types.h"
#include "editor/server_message_types.h"
#include "universe/universe.h"

namespace Lux
{

	class ReceiveTask : public MT::Task
	{
	public:
		ReceiveTask() : m_finished(false), m_allocator(10*1024) {}

		virtual int task() override;
		void stop() { m_finished = true; }

		struct EditorClientImpl* m_client;
		FIFOAllocator m_allocator;

	private:
		volatile bool m_finished;
	};


	struct EditorClientImpl
	{
		typedef MT::LockFreeQueue<uint8_t, 32> MessageQueue;

		void sendMessage(uint32_t type, const void* data, int32_t size);
		void onMessage(uint8_t* data, int size);

		Net::TCPConnector m_connector;
		Net::TCPStream* m_stream;
		ReceiveTask m_task;
		EventManager m_event_manager;
		MessageQueue m_messages;
		string m_base_path;
	};


	int ReceiveTask::task()
	{
		uint8_t data[8];
		while(!m_finished)
		{
			if(m_client->m_stream->read(&data[0], 8))
			{
				int32_t length = *(int32_t*)&data[0];
				int32_t guard = *(int32_t*)&data[4];
				ASSERT(guard == 0x12345678);
				if(length > 0)
				{
					uint8_t* msg = NULL;
					while (msg == NULL)
					{
						msg = (uint8_t*)m_allocator.allocate(length + 4);
					}
					m_client->m_stream->read(msg + 4, length);
					((int32_t*)msg)[0] = length;
					{
						while(!m_client->m_messages.push(msg));
					}
				}
			}
		}
		return 1;
	}


	void EditorClient::processMessages()
	{
		
		while (!m_impl->m_messages.isEmpty())
		{
			uint8_t* msg = m_impl->m_messages.pop();
			m_impl->onMessage(msg + 4, *(int32_t*)msg);
			m_impl->m_task.m_allocator.deallocate(msg);
		}
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
		if (m_impl)
		{
			LUX_DELETE(m_impl->m_stream);
			m_impl->m_task.stop();
			m_impl->m_task.destroy();
			LUX_DELETE(m_impl);
		}
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

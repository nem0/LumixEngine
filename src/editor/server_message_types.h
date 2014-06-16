#pragma once


#include "core/map.h"
#include "core/array.h"
#include "core/string.h"
#include "core/event_manager.h"


namespace Lumix
{


class Blob;


struct ServerMessageType
{
	enum Value
	{
		ENTITY_SELECTED = 1,
		PROPERTY_LIST = 2,
		ENTITY_POSITION = 3,
		LOG_MESSAGE = 4,
	};
};


struct LUX_ENGINE_API EntityPositionEvent : public Event
{
	EntityPositionEvent() { m_type = ServerMessageType::ENTITY_POSITION; }
	
	//virtual void write(IStream& stream) override;
	void read(Blob& stream);
	
	int32_t index;
	float x;
	float y;
	float z;
};


struct LUX_ENGINE_API EntitySelectedEvent : public Event
{
	EntitySelectedEvent() { m_type = ServerMessageType::ENTITY_SELECTED; }
	
	//virtual void write(IStream& stream) override;
	void read(Blob& stream);
	
	int32_t index;
	Array<uint32_t> components;
};


struct LUX_ENGINE_API LogEvent : public Event
{
	LogEvent() { m_type = ServerMessageType::LOG_MESSAGE; }
	
	//virtual void write(IStream& stream) override;
	void read(Blob& stream);
	
	int32_t type;
	string message;
	string system;
};


struct LUX_ENGINE_API PropertyListEvent : public Event
{
	struct Property
	{
		Property() { data = NULL; }
		~Property() { LUX_DELETE_ARRAY(data); }
		uint32_t name_hash;
		void* data;
		int32_t data_size;
	};

	PropertyListEvent() { m_type = ServerMessageType::PROPERTY_LIST; }
	
	//virtual void write(IStream& stream) override;
	void read(Blob& stream);
	
	uint32_t type_hash;
	Array<Property> properties;
};


} // ~namespace Lumix

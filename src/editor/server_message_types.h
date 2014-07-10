#pragma once


#include "core/map.h"
#include "core/array.h"
#include "core/string.h"


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


struct LUMIX_ENGINE_API EntityPositionEvent
{
	void read(Blob& stream);
	
	int32_t index;
	float x;
	float y;
	float z;
};


struct LUMIX_ENGINE_API EntitySelectedEvent
{
	void read(Blob& stream);
	
	int32_t index;
	Array<uint32_t> components;
};


struct LUMIX_ENGINE_API LogEvent
{
	void read(Blob& stream);
	
	int32_t type;
	string message;
	string system;
};


struct LUMIX_ENGINE_API PropertyListEvent
{
	struct Property
	{
		Property() { data = NULL; }
		~Property() { LUMIX_DELETE_ARRAY(data); }
		uint32_t name_hash;
		void* data;
		int32_t data_size;
	};

	void read(Blob& stream);
	
	uint32_t type_hash;
	Array<Property> properties;
};


} // ~namespace Lumix

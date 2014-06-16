#include "server_message_types.h"
#include "core/blob.h"


namespace Lux
{


void EntityPositionEvent::read(Blob& stream)
{
	stream.read(&index, sizeof(index));
	stream.read(&x, sizeof(x));
	stream.read(&y, sizeof(y));
	stream.read(&z, sizeof(z));
}


void EntitySelectedEvent::read(Blob& stream)
{
	stream.read(&index, sizeof(index));
	int32_t count;
	stream.read(&count, sizeof(count));
	components.resize(count);
	for(int i = 0; i < count; ++i)
	{
		stream.read(&components[i], sizeof(components[i]));
	}
}


void LogEvent::read(Blob& stream)
{
	stream.read(&type, sizeof(type));
	int32_t len;
	stream.read(&len, sizeof(len));
	char tmp[255];
	if(len < 255)
	{
		stream.read(tmp, len);	
		tmp[len] = 0;
	}
	system = tmp;
	stream.read(&len, sizeof(len));
	if(len < 255)
	{
		stream.read(tmp, len);	
		tmp[len] = 0;
		message = tmp;
	}
	else
	{
		char* buf = LUX_NEW_ARRAY(char, len+1);
		stream.read(buf, len);
		buf[len] = 0;
		message = buf;
		LUX_DELETE_ARRAY(buf);
	}
}


void PropertyListEvent::read(Blob& stream)
{
	int32_t count;
	stream.read(&count, sizeof(count));
	properties.resize(count);
	stream.read(&type_hash, sizeof(type_hash));
	for(int i = 0; i < count; ++i)
	{
		stream.read(&properties[i].name_hash, sizeof(properties[i].name_hash));
		stream.read(&properties[i].data_size, sizeof(properties[i].data_size));
		properties[i].data = LUX_NEW_ARRAY(uint8_t, properties[i].data_size);
		stream.read(properties[i].data, properties[i].data_size);
	}
}


} // ~namespace Lux

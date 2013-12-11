#include "json_serializer.h"
#include <cstring>
#include <cassert>


namespace Lux
{


JsonSerializer::JsonSerializer(IStream& stream, AccessMode access_mode)
	: m_stream(stream)
	, m_access_mode(access_mode)
{
	if(m_access_mode == READ)
	{
		m_stream.read(&m_buffer, 1);
	}
}


void JsonSerializer::serialize(const char* label, unsigned int value)
{
	writeBlockComma();
	char tmp[100];
	sprintf_s(tmp, "\"%s\" : %u", label, value);
	m_stream.write(tmp);
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, float value)
{
	writeBlockComma();
	char tmp[100];
	sprintf_s(tmp, "\"%s\" : %f", label, value);
	m_stream.write(tmp);
	m_is_first_in_block = false;
}



void JsonSerializer::serialize(const char* label, int value)
{
	writeBlockComma();
	char tmp[100];
	sprintf_s(tmp, "\"%s\" : %d", label, value);
	m_stream.write(tmp);
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, const char* value)
{
	writeBlockComma();
	writeString(label);
	m_stream.write(" : \"");
	m_stream.write(value);
	m_stream.write("\"");
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, bool value)
{
	writeBlockComma();
	writeString(label);
	m_stream.write(value ? " : true" : " : false");
	m_is_first_in_block = false;
}


void JsonSerializer::beginArray(const char* label)
{
	writeBlockComma();
	writeString(label);
	m_stream.write(" : [");
	m_is_first_in_block = true;
}


void JsonSerializer::endArray()
{
	m_stream.write("]");
	m_is_first_in_block = false;
}

void JsonSerializer::serializeArrayItem(const char* value)
{
	writeBlockComma();
	writeString(value);
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(string& value)
{
	writeBlockComma();
	writeString(value.c_str() ? value.c_str() : "");
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(unsigned int value)
{
	writeBlockComma();
	char tmp[20];
	sprintf_s(tmp, "%u", value); 
	m_stream.write(tmp);
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(int value)
{
	writeBlockComma();
	char tmp[20];
	sprintf_s(tmp, "%d", value); 
	m_stream.write(tmp);
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(float value)
{
	writeBlockComma();
	char tmp[20];
	sprintf_s(tmp, "%f", value); 
	m_stream.write(tmp);
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(bool value)
{
	writeBlockComma();
	m_stream.write(value ? "true" : "false");
	m_is_first_in_block = false;
}


void JsonSerializer::deserialize(const char* label, float& value)
{
	deserializeLabel(label);

	unsigned char c = m_buffer;
	value = 0;
	char tmp[20];
	int i = 0;
	if(c == '-')
	{
		tmp[i] = c;
		++i;
		m_stream.read(&c, 1);
	}
	while((c >= '0' && c <= '9') || c == '.')
	{
		tmp[i] = c;
		++i;
		m_stream.read(&c, 1);
	}
	tmp[i] = 0;
	sscanf_s(tmp, "%f", &value);
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserialize(const char* label, unsigned int& value)
{
	deserializeLabel(label);

	unsigned char c = m_buffer;
	value = 0;
	while(c >= '0' && c <= '9')
	{
		value *= 10;
		value += c - '0';
		m_stream.read(&c, 1);
	}
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserialize(const char* label, int& value)
{
	deserializeLabel(label);

	unsigned char c = m_buffer;
	value = 0;
	int sign = 1;
	if(c == '-')
	{
		sign = -1;
		m_stream.read(&c, 1);
	}
	while(c >= '0' && c <= '9')
	{
		value *= 10;
		value += c - '0';
		m_stream.read(&c, 1);
	}
	value *= sign;
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserialize(const char* label, char* value)
{
	deserializeLabel(label);

	int index = 0;
	unsigned char c;
	m_stream.read(&c, 1);
	while(c != '\"')
	{
		value[index] = c;
		m_stream.read(&c, 1);
		++index;
	}
	value[index] = 0;
	m_stream.read(&m_buffer, 1);
	skipControl();
}


void JsonSerializer::deserializeArrayBegin(const char* label)
{
	deserializeLabel(label);
	skipControl();
}


void JsonSerializer::deserializeArrayItem(char* value)
{
	unsigned char c;
	m_stream.read(&c, 1);
	char* out = value;
	while(c != '"')
	{
		*out = c;
		++out;
		m_stream.read(&c, 1);
	}
	*out = 0;
	m_stream.read(&m_buffer, 1);
	skipControl();
}


void JsonSerializer::deserializeArrayItem(string& value)
{
	unsigned char c;
	m_stream.read(&c, 1);
	char tmp[100];
	char* out = tmp;
	while(c != '"')
	{
		*out = c;
		++out;
		if(out - tmp > 98)
		{
			*out = 0;
			value += tmp;
			out = tmp;
		}
		m_stream.read(&c, 1);
	}
	*out = 0;
	value = tmp;
	m_stream.read(&m_buffer, 1);
	skipControl();
}


void JsonSerializer::deserializeArrayItem(unsigned int& value)
{
	unsigned char c = m_buffer;
	value = 0;
	while(c >= '0' && c <= '9')
	{
		value *= 10;
		value += c - '0';
		m_stream.read(&c, 1);
	}
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserializeArrayItem(int& value)
{
	unsigned char c = m_buffer;
	value = 0;
	int sign = 1;
	if(c == '-')
	{
		sign = -1;
		m_stream.read(&c, 1);
	}
	while(c >= '0' && c <= '9')
	{
		value *= 10;
		value += c - '0';
		m_stream.read(&c, 1);
	}
	value *= sign;
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserializeArrayItem(float& value)
{
	unsigned char c = m_buffer;
	value = 0;
	char tmp[20];
	int i = 0;
	if(c == '-')
	{
		tmp[i] = c;
		++i;
		m_stream.read(&c, 1);
	}
	while((c >= '0' && c <= '9') || c == '.')
	{
		tmp[i] = c;
		++i;
		m_stream.read(&c, 1);
	}
	tmp[i] = 0;
	sscanf_s(tmp, "%f", &value);
	m_buffer = c;
	skipControl();
}



void JsonSerializer::deserializeArrayItem(bool& value)
{
	unsigned char c = m_buffer;
	char tmp[20];
	int i = 0;
	while(c >= 'a' && c <= 'z')
	{
		tmp[i] = c;
		++i;
		m_stream.read(&c, 1);
	}
	tmp[i] = 0;
	value = strcmp("true", tmp) == 0;
	m_buffer = c;
	skipControl();
}


void JsonSerializer::deserialize(const char* label, bool& value)
{
	deserializeLabel(label);
	unsigned char tmp[5];
	if(m_buffer == 't')
	{
		m_stream.read(tmp, 3);
		m_buffer = tmp[2];
	}
	else
	{
		m_stream.read(tmp, 4);
		m_buffer = tmp[3];
	}
	skipControl();
}


void JsonSerializer::deserializeLabel(const char* label)
{
	unsigned char c = m_buffer;
	assert(m_buffer == '"');
	char tmp[255];
	char* to = tmp;
	do
	{
		m_stream.read(&c, 1);
		*to = c;
		++to;
	}
	while(c != '"');
	--to;
	*to = 0;
	assert(strcmp(label, tmp) == 0);
	m_stream.read(&m_buffer, 1);
	skipControl();
}


} // ~namespace lux
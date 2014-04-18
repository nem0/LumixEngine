#include "json_serializer.h"
#include <cstring>
#include <inttypes.h>
#include "core/log.h"


namespace Lux
{


	JsonSerializer::JsonSerializer(FS::IFile& file, AccessMode access_mode, const char* path)
	: m_file(file)
	, m_access_mode(access_mode)
{
	m_path = path;
	m_is_first_in_block = true;
	if(m_access_mode == READ)
	{
		m_file.read(&m_buffer, 1);
		deserializeToken();
	}
}


void JsonSerializer::serialize(const char* label, unsigned int value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	sprintf(tmp, " : %u", value);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, float value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	sprintf(tmp, " : %f", value);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}



void JsonSerializer::serialize(const char* label, int value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	sprintf(tmp, " : %d", value);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, const char* value)
{
	writeBlockComma();
	writeString(label);
	m_file.write(" : \"", 4);
	if(value == NULL)
	{
		m_file.write("", 1);
	}
	else
	{
		m_file.write(value, (int32_t)strlen(value));
	}
	m_file.write("\"", 1);
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, bool value)
{
	writeBlockComma();
	writeString(label);
	m_file.write(value ? " : true" : " : false", value ? 7 : 8);
	m_is_first_in_block = false;
}


void JsonSerializer::beginArray(const char* label)
{
	writeBlockComma();
	writeString(label);
	m_file.write(" : [", 4);
	m_is_first_in_block = true;
}


void JsonSerializer::endArray()
{
	m_file.write("]", 1);
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
	sprintf(tmp, "%u", value); 
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(int value)
{
	writeBlockComma();
	char tmp[20];
	sprintf(tmp, "%d", value); 
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(float value)
{
	writeBlockComma();
	char tmp[20];
	sprintf(tmp, "%f", value); 
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(bool value)
{
	writeBlockComma();
	m_file.write(value ? "true" : "false", value ? 4 : 5);
	m_is_first_in_block = false;
}


void JsonSerializer::deserialize(bool& value)
{
	logErrorIfNot(!m_is_string_token);
	value = strcmp(m_token, "true") == 0;
	deserializeToken();
}


void JsonSerializer::deserialize(float& value)
{
	logErrorIfNot(!m_is_string_token);
	int i = sscanf(m_token, "%f", &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}



void JsonSerializer::deserialize(int32_t& value)
{
	logErrorIfNot(!m_is_string_token);
	int i = sscanf(m_token, "%" PRIi32, &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserialize(char* value, int max_length)
{
	logErrorIfNot(m_is_string_token);
	readStringToken(value, max_length);
}


void JsonSerializer::deserialize(const char* label, float& value)
{
	deserializeLabel(label);
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%f", &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, uint32_t& value)
{
	deserializeLabel(label);
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%" PRIu32, &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


bool JsonSerializer::isObjectEnd() const
{
	return !m_is_string_token && m_token[0] == '}' && m_token[1] == '\0';
}


void JsonSerializer::deserialize(const char* label, int& value)
{
	deserializeLabel(label);
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%d", &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, char* value, int max_length)
{
	deserializeLabel(label);
	readStringToken(value, max_length);
}


void JsonSerializer::deserializeArrayBegin(const char* label)
{
	deserializeLabel(label);
	logErrorIfNot(!m_is_string_token && m_token[0] == '[' && m_token[1] == '\0');
	m_is_first_in_block = true;
	deserializeToken();
}


void JsonSerializer::deserializeArrayBegin()
{
	logErrorIfNot(!m_is_string_token && m_token[0] == '[' && m_token[1] == '\0');
	m_is_first_in_block = true;
	deserializeToken();
}



void JsonSerializer::deserializeRawString(char* buffer, int max_length)
{
	buffer[0] = m_buffer;
	m_file.read(buffer + 1, max_length);
}


void JsonSerializer::nextArrayItem()
{
	if (!m_is_first_in_block)
	{
		logErrorIfNot(!m_is_string_token && m_token[0] == ',' && m_token[1] == '\0');
		deserializeToken();
	}
}


bool JsonSerializer::isArrayEnd() const
{
	return !m_is_string_token && m_token[0] == ']' && m_token[1] == '\0';
}


void JsonSerializer::deserializeArrayEnd()
{
	logErrorIfNot(!m_is_string_token && m_token[0] == ']' && m_token[1] == '\0');
	m_is_first_in_block = false;
	deserializeToken();
}



void JsonSerializer::deserializeArrayItem(char* value, int max_length)
{
	deserializeArrayComma();
	logErrorIfNot(m_is_string_token);
	readStringToken(value, max_length);
}


void JsonSerializer::deserializeArrayItem(string& value)
{
	deserializeArrayComma();
	logErrorIfNot(m_is_string_token);
	char tmp[256];
	value = "";
	while (readStringTokenPart(tmp, 255))
	{
		value += tmp;
	}
}


void JsonSerializer::deserializeArrayItem(uint32_t& value)
{
	deserializeArrayComma();
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%" PRIu32, &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int32_t& value)
{
	deserializeArrayComma();
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%" PRIi32, &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int64_t& value)
{
	deserializeArrayComma();
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%" PRIi64, &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(float& value)
{
	deserializeArrayComma();
	logErrorIfNot(!m_is_string_token);
	int i = sscanf_s(m_token, "%f", &value);
	logErrorIfNot(i == 1);
	deserializeToken();
}



void JsonSerializer::deserializeArrayItem(bool& value)
{
	deserializeArrayComma();
	logErrorIfNot(!m_is_string_token);
	value = strcmp("true", m_token) == 0;
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, bool& value)
{
	deserializeLabel(label);
	logErrorIfNot(!m_is_string_token);
	value = strcmp("true", m_token) == 0;
	deserializeToken();
}


static bool isDelimiter(char c)
{
	return c == '\t' || c == '\n' || c == ' ' || c == '\r';
}


bool JsonSerializer::readStringTokenPart(char* tmp, int max_len)
{
	logErrorIfNot(m_is_string_token);
	int i = 0;
	while (m_file.read(tmp + i, 1) && tmp[i] != '"')
	{
		++i;
		logErrorIfNot(i < max_len);
	}
	bool is_end = tmp[i] == '"';
	tmp[i] = '\0';
	if (is_end)
	{
		if (!m_file.read(&m_buffer, 1))
		{
			return false;
		}
		while (isDelimiter(m_buffer))
		{
			if (!m_file.read(&m_buffer, 1))
			{
				return false;
			}
		}
		deserializeToken();
		return false;
	}
	else
	{
		return true;
	}
}


void JsonSerializer::readStringToken(char* tmp, int max_len)
{
	logErrorIfNot(m_is_string_token);
	int i = 0;
	while (m_file.read(tmp + i, 1) && tmp[i] != '"')
	{
		++i;
		logErrorIfNot( i < max_len);
	}
	tmp[i] = '\0';
	if (!m_file.read(&m_buffer, 1))
	{
		return;
	}
	while (isDelimiter(m_buffer))
	{
		if (!m_file.read(&m_buffer, 1))
		{
			return;
		}
	}
	deserializeToken();
}

void JsonSerializer::deserializeArrayComma()
{
	if (m_is_first_in_block)
	{
		m_is_first_in_block = false;
	}
	else
	{
		logErrorIfNot(!m_is_string_token && m_token[0] == ',' && m_token[1] == '\0');
		deserializeToken();
	}
}


static bool isToken(char c)
{
	return c == ',' || c == '[' || c == ']' || c == '{' || c == '}';
}


void JsonSerializer::deserializeToken()
{
	int i = 0;
	if (m_buffer == '"')
	{
		m_is_string_token = true;
		return;
	}
	m_is_string_token = false;
	if (isToken(m_buffer))
	{
		m_token[0] = m_buffer;
		m_token[1] = '\0';
		m_file.read(&m_buffer, 1);
	}
	else 
	{
		while (!isDelimiter(m_buffer) && !isToken(m_buffer))
		{
			m_token[i] = m_buffer;
			++i;
			if (!m_file.read(&m_buffer, 1))
			{
				break;
			}
			logErrorIfNot(i < TOKEN_MAX_SIZE); /// TODO not logErrorIfNot
		}
		m_token[i] = '\0';
	}
	while (isDelimiter(m_buffer))
	{
		if(!m_file.read(&m_buffer, 1))
		{
			return;
		}
	}
}


void JsonSerializer::deserializeObjectBegin()
{
	m_is_first_in_block = true;
	logErrorIfNot(!m_is_string_token && m_token[0] == '{' && m_token[1] == '\0');
	deserializeToken();
}

void JsonSerializer::deserializeObjectEnd()
{
	logErrorIfNot(!m_is_string_token && m_token[0] == '}' && m_token[1] == '\0');
	m_is_first_in_block = false;
	deserializeToken();
}


void JsonSerializer::deserializeLabel(char* label, int max_length)
{
	if (!m_is_first_in_block)
	{
		logErrorIfNot(!m_is_string_token && m_token[0] == ',' && m_token[1] == '\0');
		deserializeToken();
	}
	else
	{
		m_is_first_in_block = false;
	}
	logErrorIfNot(m_is_string_token);
	readStringToken(label, max_length);
	logErrorIfNot(!m_is_string_token && m_token[0] == ':' && m_token[1] == '\0');
	deserializeToken();
}


void JsonSerializer::deserializeLabel(const char* label)
{
	if(!m_is_first_in_block)
	{
		logErrorIfNot(!m_is_string_token && m_token[0] == ',' && m_token[1] == '\0');
		deserializeToken();
	}
	else
	{
		m_is_first_in_block = false;
	}
	logErrorIfNot(m_is_string_token);
	char tmp[256];
	readStringToken(tmp, 255);
	logErrorIfNot(strcmp(label, tmp) == 0);
	logErrorIfNot(!m_is_string_token && m_token[0] == ':' && m_token[1] == '\0');
	deserializeToken();
}


void JsonSerializer::logErrorIfNot(bool condition)
{
	if (!condition)
	{
		g_log_error.log("serializer", "Error parsing JSON file %s", m_path.c_str());
	}
}


} // ~namespace lux

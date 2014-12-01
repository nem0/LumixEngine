#include "json_serializer.h"
#include <inttypes.h>
#include "core/log.h"


namespace Lumix
{


JsonSerializer::JsonSerializer(FS::IFile& file, AccessMode access_mode, const char* path)
	: m_file(file)
	, m_access_mode(access_mode)
	, m_is_eof(false)
{
	m_path = path;
	m_is_first_in_block = true;
	if(m_access_mode == READ)
	{
		m_is_eof = !m_file.read(&m_buffer, 1);
		deserializeToken();
	}
}


void JsonSerializer::serialize(const char* label, unsigned int value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	toCString(value, tmp, 20);
	m_file.write(" : ", (int32_t)strlen(" : "));
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serialize(const char* label, float value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	toCString(value, tmp, 20, 8);
	m_file.write(" : ", (int32_t)strlen(" : "));
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}



void JsonSerializer::serialize(const char* label, int value)
{
	writeBlockComma();
	char tmp[20];
	writeString(label);
	toCString(value, tmp, 20);
	m_file.write(" : ", (int32_t)strlen(" : "));
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


void JsonSerializer::beginObject()
{
	writeBlockComma();
	m_file.write("{", 1);
	m_is_first_in_block = true;
}


void JsonSerializer::beginObject(const char* label)
{
	writeBlockComma();
	writeString(label);
	m_file.write(" : {", 4);
	m_is_first_in_block = true;
}

void JsonSerializer::endObject()
{
	m_file.write("}", 1);
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
	toCString(value, tmp, 20);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(int value)
{
	writeBlockComma();
	char tmp[20];
	toCString(value, tmp, 20);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(int64_t& value)
{
	writeBlockComma();
	char tmp[30];
	toCString(value, tmp, 30);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(float value)
{
	writeBlockComma();
	char tmp[20];
	toCString(value, tmp, 20, 8);
	m_file.write(tmp, (int32_t)strlen(tmp));
	m_is_first_in_block = false;
}


void JsonSerializer::serializeArrayItem(bool value)
{
	writeBlockComma();
	m_file.write(value ? "true" : "false", value ? 4 : 5);
	m_is_first_in_block = false;
}


void JsonSerializer::deserialize(bool& value, bool default_value)
{
	value = !m_is_string_token ? (strcmp(m_token, "true") == 0) : default_value;
	deserializeToken();
}


void JsonSerializer::deserialize(float& value, float default_value)
{
	if (!m_is_string_token)
	{
		value = (float)atof(m_token);
	}
	else
	{
		value = default_value;
	}
	deserializeToken();
}



void JsonSerializer::deserialize(int32_t& value, int32_t default_value)
{
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(char* value, int max_length, const char* default_value)
{
	if (!m_is_string_token || !readStringToken(value, max_length))
	{
		copyString(value, max_length, default_value);
	}
}


void JsonSerializer::deserialize(const char* label, float& value, float default_value)
{
	deserializeLabel(label);
	if (!m_is_string_token)
	{
		value = (float)atof(m_token);
	}
	else
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, uint32_t& value, uint32_t default_value)
{
	deserializeLabel(label);
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


bool JsonSerializer::isObjectEnd() const
{
	return m_is_eof || (!m_is_string_token && m_token[0] == '}' && m_token[1] == '\0');
}


void JsonSerializer::deserialize(const char* label, int32_t& value, int32_t default_value)
{
	deserializeLabel(label);
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, char* value, int max_length, const char* default_value)
{
	deserializeLabel(label);
	if (!readStringToken(value, max_length))
	{
		copyString(value, max_length, default_value);
	}
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
	strncpy(buffer, m_token, max_length);
	size_t token_length = strlen(m_token);
	if (token_length + 2 <= (size_t)max_length)
	{
		buffer[token_length] = '\n';
		buffer[token_length + 1] = m_buffer;
		m_is_eof = !m_file.read(buffer + token_length + 2, max_length - token_length - 2);
	}
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
	return m_is_eof || (!m_is_string_token && m_token[0] == ']' && m_token[1] == '\0');
}


void JsonSerializer::deserializeArrayEnd()
{
	logErrorIfNot(!m_is_string_token && m_token[0] == ']' && m_token[1] == '\0');
	m_is_first_in_block = false;
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(char* value, int max_length, const char* default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		readStringToken(value, max_length);
	}
	else
	{
		copyString(value, max_length, default_value);
	}
}


void JsonSerializer::deserializeArrayItem(string& value, const char* default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		char tmp[256];
		value = "";
		while (readStringTokenPart(tmp, 255))
		{
			value += tmp;
		}
		value += tmp;
	}
	else
	{
		value = default_value;
	}
}


void JsonSerializer::deserializeArrayItem(uint32_t& value, uint32_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int32_t& value, int32_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int64_t& value, int64_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, (int)strlen(m_token), &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(float& value, float default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		value = default_value;
	}
	else
	{
		value = (float)atof(m_token);
	}
	deserializeToken();
}



void JsonSerializer::deserializeArrayItem(bool& value, bool default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		value = default_value;
	}
	else
	{
		value = strcmp("true", m_token) == 0;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, bool& value, bool default_value)
{
	deserializeLabel(label);
	if(!m_is_string_token)
	{
		value = strcmp("true", m_token) == 0;
	}
	else
	{
		value = default_value;
	}
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
	bool read_status = m_file.read(tmp + i, 1);
	while (i < max_len && read_status && tmp[i] != '"')
	{
		++i;
		if (i < max_len)
		{
			read_status = m_file.read(tmp + i, 1);
		}
	}
	bool is_end = i < max_len || !read_status;
	tmp[i] = '\0';
	if (is_end)
	{
		if (!m_file.read(&m_buffer, 1))
		{
			m_is_eof = true;
			return false;
		}
		while (isDelimiter(m_buffer))
		{
			if (!m_file.read(&m_buffer, 1))
			{
				m_is_eof = true;
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


bool JsonSerializer::readStringToken(char* tmp, int max_len)
{
	if (!m_is_string_token)
		return false;
	int i = 0;
	while (m_file.read(tmp + i, 1) && tmp[i] != '"')
	{
		++i;
		logErrorIfNot( i < max_len);
	}
	tmp[i] = '\0';
	if (!m_file.read(&m_buffer, 1))
	{
		m_is_eof = true;
		return true;
	}
	while (isDelimiter(m_buffer))
	{
		if (!m_file.read(&m_buffer, 1))
		{
			m_is_eof = true;
			return true;
		}
	}
	deserializeToken();
	return true;
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
		m_is_eof = !m_file.read(&m_buffer, 1);
	}
	else 
	{
		int i = 0;
		while (!isDelimiter(m_buffer) && !isToken(m_buffer))
		{
			m_token[i] = m_buffer;
			++i;
			if (!m_file.read(&m_buffer, 1))
			{
				m_is_eof = true;
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
			m_is_eof = true;
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
		g_log_error.log("serializer") << "Error parsing JSON file " << m_path.c_str();
	}
}


} // ~namespace Lumix

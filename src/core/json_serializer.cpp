#include "json_serializer.h"
#include "core/fs/ifile_device.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/path.h"
#include <inttypes.h>


namespace Lumix
{


JsonSerializer::ErrorProxy::ErrorProxy(JsonSerializer& serializer)
	: m_log(g_log_error, "serializer", serializer.m_allocator)
{
	const char* c = serializer.m_data;
	int line = 0;
	int column = 0;
	while (c < serializer.m_token)
	{
		if (*c == '\n')
		{
			++line;
			column = 0;
		}
		++column;
		++c;
	}

	m_log << serializer.m_path.c_str() << "(line "<< (line + 1) << ", column " << column << "): ";
}


JsonSerializer::JsonSerializer(FS::IFile& file, AccessMode access_mode, const char* path, IAllocator& allocator)
	: m_file(file)
	, m_access_mode(access_mode)
	, m_error_message(allocator)
	, m_allocator(allocator)
{
	m_is_error = false;
	m_path = path;
	m_is_first_in_block = true;
	m_data = nullptr;
	m_is_string_token = false;
	if(m_access_mode == READ)
	{
		m_data_size = file.size();
		if (file.getBuffer() != nullptr)
		{
			m_data = (const char*)file.getBuffer();
			m_own_data = false;
		}
		else
		{
			int size = m_file.size();
			char* data = (char*)m_allocator.allocate(size);
			m_own_data = true;
			file.read(data, m_data_size);
			m_data = data;
		}
		m_token = m_data;
		m_token_size = 0;
		deserializeToken();
	}
}


JsonSerializer::~JsonSerializer()
{
	if (m_access_mode == READ && m_own_data)
	{
		m_allocator.deallocate((void*)m_data);
	}
}


#pragma region serialization

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
	if(value == nullptr)
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


void JsonSerializer::serializeArrayItem(int64_t value)
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

#pragma endregion


#pragma region deserialization


void JsonSerializer::deserialize(bool& value, bool default_value)
{
	value = !m_is_string_token ? m_token_size == 4 && (strncmp(m_token, "true", 4) == 0) : default_value;
	deserializeToken();
}


void JsonSerializer::deserialize(float& value, float default_value)
{
	if (!m_is_string_token)
	{
		value = tokenToFloat();
	}
	else
	{
		value = default_value;
	}
	deserializeToken();
}



void JsonSerializer::deserialize(int32_t& value, int32_t default_value)
{
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(char* value, int max_length, const char* default_value)
{
	if (!m_is_string_token)
	{
		copyString(value, max_length, default_value);
	}
	else
	{
		int size = Math::minValue(max_length - 1, m_token_size);
		memcpy(value, m_token, size);
		value[size] = '\0';
		deserializeToken();
	}
}


void JsonSerializer::deserialize(const char* label, float& value, float default_value)
{
	deserializeLabel(label);
	if (!m_is_string_token)
	{
		value = tokenToFloat();
		deserializeToken();
	}
	else
	{
		value = default_value;
	}
}


void JsonSerializer::deserialize(const char* label, uint32_t& value, uint32_t default_value)
{
	deserializeLabel(label);
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
	{
		value = default_value;
	}
	else
	{
		deserializeToken();
	}
}


bool JsonSerializer::isObjectEnd()
{
	if (m_token == m_data + m_data_size)
	{
		error().log() << "Unexpected end of file while looking for the end of an object.";
		return true;
	}

	return (!m_is_string_token && m_token_size == 1 && m_token[0] == '}');
}


void JsonSerializer::deserialize(const char* label, int32_t& value, int32_t default_value)
{
	deserializeLabel(label);
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, char* value, int max_length, const char* default_value)
{
	deserializeLabel(label);
	if (!m_is_string_token)
	{
		copyString(value, max_length, default_value);
	}
	else
	{
		int size = Math::minValue(max_length - 1, m_token_size);
		memcpy(value, m_token, size);
		value[size] = '\0';
		deserializeToken();
	}
}


void JsonSerializer::deserializeArrayBegin(const char* label)
{
	deserializeLabel(label);
	expectToken('[');
	m_is_first_in_block = true;
	deserializeToken();
}


void JsonSerializer::expectToken(char expected_token)
{
	if (m_is_string_token || m_token_size != 1 || m_token[0] != expected_token)
	{
		char tmp[2];
		tmp[0] = expected_token;
		tmp[1] = 0;
		error().log() << "Unexpected token \"" << string(m_token, m_token_size, m_allocator) << "\", expected " << tmp << ".";
		deserializeToken();
	}
}


void JsonSerializer::deserializeArrayBegin()
{
	expectToken('[');
	m_is_first_in_block = true;
	deserializeToken();
}



void JsonSerializer::deserializeRawString(char* buffer, int max_length)
{
	int size = Math::minValue(max_length - 1, m_token_size);
	memcpy(buffer, m_token, size);
	buffer[size] = '\0';
	deserializeToken();
}


void JsonSerializer::nextArrayItem()
{
	if (!m_is_first_in_block)
	{
		expectToken(',');
		deserializeToken();
	}
}


bool JsonSerializer::isArrayEnd()
{
	if (m_token == m_data + m_data_size)
	{
		error().log() << "Unexpected end of file while looking for the end of an array.";
		return true;
	}

	return (!m_is_string_token && m_token_size == 1 && m_token[0] == ']');
}


void JsonSerializer::deserializeArrayEnd()
{
	expectToken(']');
	m_is_first_in_block = false;
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(char* value, int max_length, const char* default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		int size = Math::minValue(max_length - 1, m_token_size);
		memcpy(value, m_token, size);
		value[size] = '\0';
		deserializeToken();
	}
	else
	{
		error().log() << "Unexpected token \"" << string(m_token, m_token_size, m_allocator) << "\", expected string.";
		deserializeToken();
		copyString(value, max_length, default_value);
	}
}


void JsonSerializer::deserializeArrayItem(string& value, const char* default_value)
{
	deserializeArrayComma();
	if (m_is_string_token)
	{
		value.set(m_token, m_token_size);
		deserializeToken();
	}
	else
	{
		value = default_value;
	}
}


void JsonSerializer::deserializeArrayItem(uint32_t& value, uint32_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int32_t& value, int32_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
	{
		value = default_value;
	}
	deserializeToken();
}


void JsonSerializer::deserializeArrayItem(int64_t& value, int64_t default_value)
{
	deserializeArrayComma();
	if (m_is_string_token || !fromCString(m_token, m_token_size, &value))
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
		value = tokenToFloat();
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
		value = m_token_size == 4 && strncmp("true", m_token, m_token_size) == 0;
	}
	deserializeToken();
}


void JsonSerializer::deserialize(const char* label, bool& value, bool default_value)
{
	deserializeLabel(label);
	if(!m_is_string_token)
	{
		value = m_token_size == 4 && strncmp("true", m_token, 4) == 0;
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


void JsonSerializer::deserializeArrayComma()
{
	if (m_is_first_in_block)
	{
		m_is_first_in_block = false;
	}
	else
	{
		
		expectToken(',');
		deserializeToken();
	}
}


static bool isSingleCharToken(char c)
{
	return c == ',' || c == '[' || c == ']' || c == '{' || c == '}' || c == ':';
}


void JsonSerializer::deserializeToken()
{
	m_token += m_token_size;
	if (m_is_string_token)
	{
		++m_token;
	}

	while (m_token < m_data + m_data_size && isDelimiter(*m_token))
	{
		++m_token;
	}
	if (*m_token == '/' && m_token < m_data + m_data_size - 1 && m_token[1] == '/')
	{
		m_token_size = (m_data + m_data_size) - m_token;
		m_is_string_token = false;
	}
	else if (*m_token == '"')
	{
		++m_token;
		m_is_string_token = true;
		const char* token_end = m_token;
		while (token_end < m_data + m_data_size && *token_end != '"')
		{
			++token_end;
		}
		if (token_end == m_data + m_data_size)
		{
			error().log() << "Unexpected end of file while looking for \".";
			m_token_size = 0;
		}
		m_token_size = token_end - m_token;
	}
	else if (isSingleCharToken(*m_token))
	{
		m_is_string_token = false;
		m_token_size = 1;
	}
	else
	{
		m_is_string_token = false;
		const char* token_end = m_token;
		while (token_end < m_data + m_data_size && !isDelimiter(*token_end) && !isSingleCharToken(*token_end))
		{
			++token_end;
		}
		m_token_size = token_end - m_token;
	}
}


void JsonSerializer::deserializeObjectBegin()
{
	m_is_first_in_block = true;
	expectToken('{');
	deserializeToken();
}

void JsonSerializer::deserializeObjectEnd()
{
	expectToken('}');
	m_is_first_in_block = false;
	deserializeToken();
}


void JsonSerializer::deserializeLabel(char* label, int max_length)
{
	if (!m_is_first_in_block)
	{
		expectToken(',');
		deserializeToken();
	}
	else
	{
		m_is_first_in_block = false;
	}
	if (!m_is_string_token)
	{
		error().log() << "Unexpected token \"" << string(m_token, m_token_size, m_allocator) << "\", expected string.";
		deserializeToken();
	}
	int size = Math::minValue(max_length - 1, m_token_size);
	copyString(label, size, m_token);
	label[size] = '\0';
	deserializeToken();
	expectToken(':');
	deserializeToken();
}


JsonSerializer::ErrorProxy JsonSerializer::error()
{
	m_is_error = true;
	return ErrorProxy(*this);
}


void JsonSerializer::deserializeLabel(const char* label)
{
	if(!m_is_first_in_block)
	{
		expectToken(',');
		deserializeToken();
	}
	else
	{
		m_is_first_in_block = false;
	}
	if (!m_is_string_token)
	{
		error().log() << "Unexpected token \"" << string(m_token, m_token_size, m_allocator) << "\", expected string.";
		deserializeToken();
	}
	if (strncmp(label, m_token, m_token_size) != 0)
	{
		error().log() << "Unexpected label \"" << string(m_token, m_token_size, m_allocator) << "\", expected \"" << label << "\".";
		deserializeToken();
	}
	deserializeToken();
	if (m_is_string_token || m_token_size != 1 || m_token[0] != ':')
	{
		error().log() << "Unexpected label \"" << string(m_token, m_token_size, m_allocator) << "\", expected \"" << label << "\".";
		deserializeToken();
	}
	deserializeToken();
}


#pragma endregion


float JsonSerializer::tokenToFloat()
{
	char tmp[64];
	int size = Math::minValue((int)sizeof(tmp) - 1, m_token_size);
	memcpy(tmp, m_token, size);
	tmp[size] = '\0';
	return (float)atof(tmp);
}


} // ~namespace Lumix

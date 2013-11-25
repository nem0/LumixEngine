#include "json_object.h"
#include <cstring>
#include <cstdio>


JsonObject::JsonObject(int m_token_idx, char* data, jsmntok_t* m_tokens)
{
	this->m_token_idx = m_token_idx;
	this->m_data = data;
	this->m_tokens = m_tokens;
}


JsonObject JsonObject::operator[](const char* name) const
{
	return getProperty(name);
}


JsonObject JsonObject::operator[](int index) const
{
	return getArrayItem(index);
}


JsonObject::operator unsigned int() const
{
	if(m_token_idx == -1 || m_tokens[m_token_idx].type != JSMN_PRIMITIVE)
	{
		return 0;
	}
	unsigned int tmp;
	sscanf_s(getStart(), "%u", &tmp);
	return tmp;
}


int JsonObject::skip(int index) const
{
	switch(m_tokens[index].type)
	{
		case JSMN_STRING:
		case JSMN_PRIMITIVE:
			return index + 1;
		case JSMN_ARRAY:
		case JSMN_OBJECT:
			{
				int i = index + 1;
				int end = m_tokens[index].end;
				while(m_tokens[i].start < end)
				{
					++i;
				}
				return i;
			}
	}
	return -1;
}


JsonObject JsonObject::getProperty(const char* name) const
{
	if(m_token_idx == -1 || m_tokens[m_token_idx].type != JSMN_OBJECT)
	{
		return JsonObject(-1, m_data, m_tokens);
	}
	int idx = m_token_idx + 1;
	int len = strlen(name);
	while(m_tokens[idx].start < m_tokens[m_token_idx].end)
	{
		if(strncmp(m_data + m_tokens[idx].start, name, len) == 0)
		{
			switch(m_tokens[idx + 1].type)
			{
				case JSMN_ARRAY:
					return JsonObject(idx + 1, m_data, m_tokens);
				case JSMN_OBJECT:
					return JsonObject(idx + 1, m_data, m_tokens);
				case JSMN_STRING:
					return JsonObject(idx + 1, m_data, m_tokens);
				case JSMN_PRIMITIVE:
					return JsonObject(idx + 1, m_data, m_tokens);
			}
		}
		idx = skip(idx + 1);
	}
	return JsonObject(-1, m_data, m_tokens);
}


bool JsonObject::toString(char* str, int max_size) const
{
	if(m_token_idx == -1 || m_tokens[m_token_idx].type != JSMN_STRING)
	{
		return false;
	}
	strncpy_s(str, max_size, m_data + m_tokens[m_token_idx].start, m_tokens[m_token_idx].end - m_tokens[m_token_idx].start);
	return true;
}


JsonObject JsonObject::getArrayItem(int index) const
{
	if(m_token_idx == -1 || m_tokens[m_token_idx].type != JSMN_ARRAY)
	{
		return JsonObject(-1, m_data, m_tokens);
	}
	int idx = m_token_idx + 1;
	int countdown = index;
	while(m_tokens[idx].start < m_tokens[m_token_idx].end)
	{
		if(countdown == 0)
		{
			switch(m_tokens[idx].type)
			{
				case JSMN_ARRAY:
					return JsonObject(idx, m_data, m_tokens);
				case JSMN_OBJECT:
					return JsonObject(idx, m_data, m_tokens);
				case JSMN_STRING:
					return JsonObject(idx, m_data, m_tokens);
				case JSMN_PRIMITIVE:
					return JsonObject(idx, m_data, m_tokens);
			}
		}
		--countdown;
		idx = skip(idx);
	}
	return JsonObject(-1, m_data, m_tokens);		
}


char* JsonObject::getStart() const
{
	return m_data + m_tokens[m_token_idx].start;
}
	

int JsonObject::getLength() const
{
	return m_tokens[m_token_idx].end - m_tokens[m_token_idx].start;
}


bool JsonObject::isString() const
{
	return m_token_idx >= 0 && m_tokens[m_token_idx].type == JSMN_STRING;
}

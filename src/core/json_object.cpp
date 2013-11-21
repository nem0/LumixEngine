#include "json_object.h"
#include <cstring>
#include <cstdio>


JsonObject::JsonObject(int token_idx, char* data, jsmntok_t* tokens)
{
	this->token_idx = token_idx;
	this->data = data;
	this->tokens = tokens;
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
	if(token_idx == -1 || tokens[token_idx].type != JSMN_PRIMITIVE)
	{
		return 0;
	}
	unsigned int tmp;
	sscanf_s(getStart(), "%u", &tmp);
	return tmp;
}


int JsonObject::skip(int index) const
{
	switch(tokens[index].type)
	{
		case JSMN_STRING:
		case JSMN_PRIMITIVE:
			return index + 1;
		case JSMN_ARRAY:
		case JSMN_OBJECT:
			{
				int i = index + 1;
				int end = tokens[index].end;
				while(tokens[i].start < end)
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
	if(token_idx == -1 || tokens[token_idx].type != JSMN_OBJECT)
	{
		return JsonObject(-1, data, tokens);
	}
	int idx = token_idx + 1;
	int len = strlen(name);
	while(tokens[idx].start < tokens[token_idx].end)
	{
		if(strncmp(data + tokens[idx].start, name, len) == 0)
		{
			switch(tokens[idx + 1].type)
			{
				case JSMN_ARRAY:
					return JsonObject(idx + 1, data, tokens);
				case JSMN_OBJECT:
					return JsonObject(idx + 1, data, tokens);
				case JSMN_STRING:
					return JsonObject(idx + 1, data, tokens);
				case JSMN_PRIMITIVE:
					return JsonObject(idx + 1, data, tokens);
			}
		}
		idx = skip(idx + 1);
	}
	return JsonObject(-1, data, tokens);
}


bool JsonObject::toString(char* str, int max_size) const
{
	if(token_idx == -1 || tokens[token_idx].type != JSMN_STRING)
	{
		return false;
	}
	strncpy_s(str, max_size, data + tokens[token_idx].start, tokens[token_idx].end - tokens[token_idx].start);
	return true;
}


JsonObject JsonObject::getArrayItem(int index) const
{
	if(token_idx == -1 || tokens[token_idx].type != JSMN_ARRAY)
	{
		return JsonObject(-1, data, tokens);
	}
	int idx = token_idx + 1;
	int countdown = index;
	while(tokens[idx].start < tokens[token_idx].end)
	{
		if(countdown == 0)
		{
			switch(tokens[idx].type)
			{
				case JSMN_ARRAY:
					return JsonObject(idx, data, tokens);
				case JSMN_OBJECT:
					return JsonObject(idx, data, tokens);
				case JSMN_STRING:
					return JsonObject(idx, data, tokens);
				case JSMN_PRIMITIVE:
					return JsonObject(idx, data, tokens);
			}
		}
		--countdown;
		idx = skip(idx);
	}
	return JsonObject(-1, data, tokens);		
}


char* JsonObject::getStart() const
{
	return data + tokens[token_idx].start;
}
	

int JsonObject::getLength() const
{
	return tokens[token_idx].end - tokens[token_idx].start;
}


bool JsonObject::isString() const
{
	return token_idx >= 0 && tokens[token_idx].type == JSMN_STRING;
}

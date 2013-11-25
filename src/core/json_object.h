#pragma once


#include "core/lux.h"
#include "jsmn.h"


class LUX_CORE_API JsonObject
{
	public:
		JsonObject(int token_idx, char* data, jsmntok_t* tokens);

		JsonObject operator[](const char* name) const;
		JsonObject operator[](int index) const;
		operator unsigned int() const; 
		JsonObject getProperty(const char* name) const;
		JsonObject getArrayItem(int index) const;
		bool isString() const;
		char* getStart() const;	
		int getLength() const;
		bool toString(char* str, int max_size) const;

	private:
		int skip(int index) const;

	private:
		int m_token_idx;
		char* m_data;
		jsmntok_t* m_tokens;
};

#pragma once


#include "lux.h"
#include "string.h"


namespace Lux
{


	class LUX_CORE_API ISerializer abstract
	{
		public:
			virtual void serialize(const char* label, uint32_t value) = 0;
			virtual void serialize(const char* label, float value) = 0;
			virtual void serialize(const char* label, int32_t value) = 0;
			virtual void serialize(const char* label, const char* value) = 0;
			virtual void serialize(const char* label, bool value) = 0;
			virtual void beginObject() = 0;
			virtual void beginObject(const char* label) = 0;
			virtual void endObject() = 0;
			virtual void beginArray(const char* label) = 0;
			virtual void endArray() = 0;
			virtual void serializeArrayItem(uint32_t value) = 0;
			virtual void serializeArrayItem(int32_t value) = 0;
			virtual void serializeArrayItem(int64_t& value) = 0;
			virtual void serializeArrayItem(float value) = 0;
			virtual void serializeArrayItem(bool value) = 0;
			virtual void serializeArrayItem(const char* value) = 0;
			virtual void serializeArrayItem(string& value) = 0;

			virtual void deserialize(const char* label, uint32_t& value) = 0;
			virtual void deserialize(const char* label, float& value) = 0;
			virtual void deserialize(const char* label, int32_t& value) = 0;
			virtual void deserialize(const char* label, char* value, int max_length) = 0;
			virtual void deserialize(const char* label, bool& value) = 0;
			virtual void deserialize(char* value, int max_length) = 0;
			virtual void deserialize(bool& value) = 0;
			virtual void deserialize(float& value) = 0;
			virtual void deserialize(int32_t& value) = 0;
			virtual void deserializeArrayBegin(const char* label) = 0;
			virtual void deserializeArrayBegin() = 0;
			virtual void deserializeArrayEnd() = 0;
			virtual bool isArrayEnd() const = 0;
			virtual void deserializeArrayItem(uint32_t& value) = 0;
			virtual void deserializeArrayItem(int32_t& value) = 0;
			virtual void deserializeArrayItem(int64_t& value) = 0;
			virtual void deserializeArrayItem(float& value) = 0;
			virtual void deserializeArrayItem(bool& value) = 0;
			virtual void deserializeArrayItem(char* value, int max_length) = 0;
			virtual void deserializeArrayItem(string& value) = 0;
			virtual void deserializeObjectBegin() = 0;
			virtual void deserializeObjectEnd() = 0;
			virtual void deserializeLabel(char* label, int max_length) = 0;
			virtual void deserializeRawString(char* buffer, int max_length) = 0;
			virtual void nextArrayItem() = 0;
			virtual bool isObjectEnd() const = 0;
	};


} // ~namespace Lux
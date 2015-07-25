#pragma once


#include "core/lumix.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/path.h"
#include "core/string.h"
#include <map>
#include <vector>


namespace Lumix
{


	class LUMIX_ENGINE_API JsonSerializer
	{
		public:
			class ErrorProxy
			{
				public:
					ErrorProxy(JsonSerializer& serializer);
					LogProxy& log() { return m_log; }
				
				private:
					LogProxy m_log;
			};

			enum AccessMode
			{
				READ,
				WRITE
			};
			
		public:
			JsonSerializer(FS::IFile& file, AccessMode access_mode, const char* path, IAllocator& allocator);
			~JsonSerializer();

			// serialize
			void serialize(const char* label, uint32_t value);
			void serialize(const char* label, float value);
			void serialize(const char* label, int32_t value);
			void serialize(const char* label, const char* value);
			void serialize(const char* label, bool value);
			void beginObject();
			void beginObject(const char* label);
			void endObject();
			void beginArray(const char* label);
			void endArray();
			void serializeArrayItem(uint32_t value);
			void serializeArrayItem(int32_t value);
			void serializeArrayItem(int64_t value);
			void serializeArrayItem(float value);
			void serializeArrayItem(bool value);
			void serializeArrayItem(const char* value);
			void serializeArrayItem(string& value);

			// deserialize		
			void deserialize(const char* label, uint32_t& value, uint32_t default_value);
			void deserialize(const char* label, float& value, float default_value);
			void deserialize(const char* label, int32_t& value, int32_t default_value);
			void deserialize(const char* label, char* value, int max_length, const char* default_value);
			void deserialize(const char* label, bool& value, bool default_value);
			void deserialize(char* value, int max_length, const char* default_value);
			void deserialize(bool& value, bool default_value);
			void deserialize(float& value, float default_value);
			void deserialize(int32_t& value, int32_t default_value);
			void deserializeArrayBegin(const char* label);
			void deserializeArrayBegin();
			void deserializeArrayEnd();
			bool isArrayEnd();
			void deserializeArrayItem(uint32_t& value, uint32_t default_value);
			void deserializeArrayItem(int32_t& value, int32_t default_value);
			void deserializeArrayItem(int64_t& value, int64_t default_value);
			void deserializeArrayItem(float& value, float default_value);
			void deserializeArrayItem(bool& value, bool default_value);
			void deserializeArrayItem(char* value, int max_length, const char* default_value);
			void deserializeArrayItem(string& value, const char* default_value);
			void deserializeObjectBegin();
			void deserializeObjectEnd();
			void deserializeLabel(char* label, int max_length);
			void deserializeRawString(char* buffer, int max_length);
			void nextArrayItem();
			bool isObjectEnd();

			size_t getRestOfFileSize()
			{
				const size_t NEW_LINE_AND_NULL_TERMINATION_SIZE = sizeof(char) + sizeof(char);
				return m_file.size() - m_file.pos() + NEW_LINE_AND_NULL_TERMINATION_SIZE + strlen(m_token);
			}

			bool isError() const { return m_is_error; }

		private:
			void deserializeLabel(const char* label);
			void deserializeToken();
			void deserializeArrayComma();
			float tokenToFloat();
			ErrorProxy error();
			void expectToken(char expected_token);

			inline void writeString(const char* str)
			{
				m_file.write("\"", 1);
				m_file.write(str, (int32_t)strlen(str));
				m_file.write("\"", 1);
			}

			inline void writeBlockComma()
			{
				if(!m_is_first_in_block)
				{
					m_file.write(",\n", 2);
				}
			}

		private:
			void operator=(const JsonSerializer&);
			JsonSerializer(const JsonSerializer&);

		private:
			AccessMode m_access_mode;
			bool m_is_first_in_block;
			FS::IFile& m_file;
			const char* m_token;
			int m_token_size;
			bool m_is_string_token;
			Path m_path;
			IAllocator& m_allocator;
			string m_error_message;
			
			const char* m_data;
			int m_data_size;
			bool m_own_data;
			bool m_is_error;
	};


} // !namespace Lumix

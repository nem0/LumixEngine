#pragma once


#include "core/lumix.h"
#include <map>
#include <vector>
#include "core/iserializer.h"
#include "core/fs/ifile.h"
#include "core/path.h"
#include "core/string.h"


namespace Lumix
{


	class LUMIX_CORE_API JsonSerializer : public ISerializer
	{
		public:
			enum AccessMode
			{
				READ,
				WRITE
			};
			
			static const int TOKEN_MAX_SIZE = 256;

		public:
			JsonSerializer(FS::IFile& file, AccessMode access_mode, const char* path);

			// serialize
			virtual void serialize(const char* label, uint32_t value) override;
			virtual void serialize(const char* label, float value) override;
			virtual void serialize(const char* label, int32_t value) override;
			virtual void serialize(const char* label, const char* value) override;
			virtual void serialize(const char* label, bool value) override;
			virtual void beginObject() override;
			virtual void beginObject(const char* label) override;
			virtual void endObject() override;
			virtual void beginArray(const char* label) override;
			virtual void endArray() override;
			virtual void serializeArrayItem(uint32_t value) override;
			virtual void serializeArrayItem(int32_t value) override;
			virtual void serializeArrayItem(int64_t& value) override;
			virtual void serializeArrayItem(float value) override;
			virtual void serializeArrayItem(bool value) override;
			virtual void serializeArrayItem(const char* value) override;
			virtual void serializeArrayItem(string& value) override;

			// deserialize		
			virtual void deserialize(const char* label, uint32_t& value, uint32_t default_value) override;
			virtual void deserialize(const char* label, float& value, float default_value) override;
			virtual void deserialize(const char* label, int32_t& value, int32_t default_value) override;
			virtual void deserialize(const char* label, char* value, int max_length, const char* default_value) override;
			virtual void deserialize(const char* label, bool& value, bool default_value) override;
			virtual void deserialize(char* value, int max_length, const char* default_value) override;
			virtual void deserialize(bool& value, bool default_value) override;
			virtual void deserialize(float& value, float default_value) override;
			virtual void deserialize(int32_t& value, int32_t default_value) override;
			virtual void deserializeArrayBegin(const char* label) override;
			virtual void deserializeArrayBegin() override;
			virtual void deserializeArrayEnd() override;
			virtual bool isArrayEnd() const override;
			virtual void deserializeArrayItem(uint32_t& value) override;
			virtual void deserializeArrayItem(int32_t& value) override;
			virtual void deserializeArrayItem(int64_t& value) override;
			virtual void deserializeArrayItem(float& value) override;
			virtual void deserializeArrayItem(bool& value) override;
			virtual void deserializeArrayItem(char* value, int max_length) override;
			virtual void deserializeArrayItem(string& value) override;
			virtual void deserializeObjectBegin() override;
			virtual void deserializeObjectEnd() override;
			virtual void deserializeLabel(char* label, int max_length) override;
			virtual void deserializeRawString(char* buffer, int max_length) override;
			virtual void nextArrayItem() override;
			virtual bool isObjectEnd() const override;

			size_t getRestOfFileSize()
			{
				const size_t NEW_LINE_AND_M_BUFFER_SIZE = sizeof('\n') + sizeof(m_buffer);
				return m_file.size() - m_file.pos() + NEW_LINE_AND_M_BUFFER_SIZE + strlen(m_token);
			}

		private:
			void deserializeLabel(const char* label);
			void deserializeToken();
			bool readStringToken(char* tmp, int max_len);
			bool readStringTokenPart(char* tmp, int max_len);
			void deserializeArrayComma();
			void logErrorIfNot(bool condition);

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
			unsigned char m_buffer;
			bool m_is_first_in_block;
			unsigned char* m_data;
			FS::IFile& m_file;
			char m_token[TOKEN_MAX_SIZE];
			bool m_is_string_token;
			Path m_path;
	};


} // !namespace Lumix

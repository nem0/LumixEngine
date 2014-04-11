#pragma once


#include "core/lux.h"
#include <map>
#include <vector>
#include "core/iserializer.h"
#include "core/ifile.h"
#include "core/string.h"


namespace Lux
{


	class LUX_CORE_API JsonSerializer : public ISerializer
	{
		public:
			enum AccessMode
			{
				READ,
				WRITE
			};

		public:
			JsonSerializer(FS::IFile& file, AccessMode access_mode);

			// serialize
			virtual void serialize(const char* label, uint32_t value) override;
			virtual void serialize(const char* label, float value) override;
			virtual void serialize(const char* label, int32_t value) override;
			virtual void serialize(const char* label, const char* value) override;
			virtual void serialize(const char* label, bool value) override;
			virtual void beginArray(const char* label) override;
			virtual void endArray() override;
			virtual void serializeArrayItem(uint32_t value) override;
			virtual void serializeArrayItem(int32_t value) override;
			virtual void serializeArrayItem(float value) override;
			virtual void serializeArrayItem(bool value) override;
			virtual void serializeArrayItem(const char* value) override;
			virtual void serializeArrayItem(string& value) override;

			// deserialize		
			virtual void deserialize(const char* label, uint32_t& value) override;
			virtual void deserialize(const char* label, float& value) override;
			virtual void deserialize(const char* label, int32_t& value) override;
			virtual void deserialize(const char* label, char* value, int max_length) override;
			virtual void deserialize(const char* label, bool& value) override;
			virtual void deserializeArrayBegin(const char* label) override;
			virtual void deserializeArrayEnd() override { skipControl(); }
			virtual void deserializeArrayItem(uint32_t& value) override;
			virtual void deserializeArrayItem(int32_t& value) override;
			virtual void deserializeArrayItem(int64_t& value) override;
			virtual void deserializeArrayItem(float& value) override;
			virtual void deserializeArrayItem(bool& value) override;
			virtual void deserializeArrayItem(char* value, int max_length) override;
			virtual void deserializeArrayItem(string& value) override;
		
		private:
			void deserializeLabel(const char* label);

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

			inline void skipControl()
			{
				unsigned char c = m_buffer;
				while(c == ','|| c == ' ' || c == '\t' || c == '{' || c == '[' || c == '\n' || c == '\r' || c == ':' || c == ']' || c == '}')
				{
					if(!m_file.read(&c, 1))
						return;
				}
				m_buffer = c;
			}

		private:
			void operator=(const JsonSerializer&);
			AccessMode m_access_mode;
			unsigned char m_buffer;
			bool m_is_first_in_block;
			unsigned char* m_data;
			FS::IFile& m_file;
	};


} // !namespace lux
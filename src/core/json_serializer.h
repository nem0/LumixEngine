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
			virtual void serialize(const char* label, uint32_t value) LUX_OVERRIDE;
			virtual void serialize(const char* label, float value) LUX_OVERRIDE;
			virtual void serialize(const char* label, int32_t value) LUX_OVERRIDE;
			virtual void serialize(const char* label, const char* value) LUX_OVERRIDE;
			virtual void serialize(const char* label, bool value) LUX_OVERRIDE;
			virtual void beginArray(const char* label) LUX_OVERRIDE;
			virtual void endArray() LUX_OVERRIDE;
			virtual void serializeArrayItem(uint32_t value) LUX_OVERRIDE;
			virtual void serializeArrayItem(int32_t value) LUX_OVERRIDE;
			virtual void serializeArrayItem(float value) LUX_OVERRIDE;
			virtual void serializeArrayItem(bool value) LUX_OVERRIDE;
			virtual void serializeArrayItem(const char* value) LUX_OVERRIDE;
			virtual void serializeArrayItem(string& value) LUX_OVERRIDE;

			// deserialize		
			virtual void deserialize(const char* label, uint32_t& value) LUX_OVERRIDE;
			virtual void deserialize(const char* label, float& value) LUX_OVERRIDE;
			virtual void deserialize(const char* label, int32_t& value) LUX_OVERRIDE;
			virtual void deserialize(const char* label, char* value, int max_length) LUX_OVERRIDE;
			virtual void deserialize(const char* label, bool& value) LUX_OVERRIDE;
			virtual void deserializeArrayBegin(const char* label) LUX_OVERRIDE;
			virtual void deserializeArrayEnd() LUX_OVERRIDE { skipControl(); }
			virtual void deserializeArrayItem(uint32_t& value) LUX_OVERRIDE;
			virtual void deserializeArrayItem(int32_t& value) LUX_OVERRIDE;
			virtual void deserializeArrayItem(float& value) LUX_OVERRIDE;
			virtual void deserializeArrayItem(bool& value) LUX_OVERRIDE;
			virtual void deserializeArrayItem(char* value, int max_length) LUX_OVERRIDE;
			virtual void deserializeArrayItem(string& value) LUX_OVERRIDE;
		
		private:
			void deserializeLabel(const char* label);

			inline void writeString(const char* str)
			{
				m_file.write("\"", 1);
				m_file.write(str, strlen(str));
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
			AccessMode m_access_mode;
			unsigned char m_buffer;
			bool m_is_first_in_block;
			unsigned char* m_data;
			FS::IFile& m_file;
	};


} // !namespace lux
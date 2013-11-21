#pragma once


#include "core/lux.h"
#include <map>
#include <vector>
#include "istream.h"
#include "core/string.h"
#include "core/iserializer.h"


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
		JsonSerializer(IStream& stream, AccessMode access_mode);

		// serialize
		virtual void serialize(const char* label, unsigned int value) LUX_OVERRIDE;
		virtual void serialize(const char* label, float value) LUX_OVERRIDE;
		virtual void serialize(const char* label, int value) LUX_OVERRIDE;
		virtual void serialize(const char* label, const char* value) LUX_OVERRIDE;
		virtual void serialize(const char* label, bool value) LUX_OVERRIDE;
		virtual void beginArray(const char* label) LUX_OVERRIDE;
		virtual void endArray() LUX_OVERRIDE;
		virtual void serializeArrayItem(unsigned int value) LUX_OVERRIDE;
		virtual void serializeArrayItem(int value) LUX_OVERRIDE;
		virtual void serializeArrayItem(float value) LUX_OVERRIDE;
		virtual void serializeArrayItem(bool value) LUX_OVERRIDE;
		virtual void serializeArrayItem(const char* value) LUX_OVERRIDE;
		virtual void serializeArrayItem(string& value) LUX_OVERRIDE;

		// deserialize		
		virtual void deserialize(const char* label, unsigned int& value) LUX_OVERRIDE;
		virtual void deserialize(const char* label, float& value) LUX_OVERRIDE;
		virtual void deserialize(const char* label, int& value) LUX_OVERRIDE;
		virtual void deserialize(const char* label, char* value) LUX_OVERRIDE;
		virtual void deserialize(const char* label, bool& value) LUX_OVERRIDE;
		virtual void deserializeArrayBegin(const char* label) LUX_OVERRIDE;
		virtual void deserializeArrayEnd() LUX_OVERRIDE { skipControl(); }
		virtual void deserializeArrayItem(unsigned int& value) LUX_OVERRIDE;
		virtual void deserializeArrayItem(int& value) LUX_OVERRIDE;
		virtual void deserializeArrayItem(float& value) LUX_OVERRIDE;
		virtual void deserializeArrayItem(bool& value) LUX_OVERRIDE;
		virtual void deserializeArrayItem(char* value) LUX_OVERRIDE;
		virtual void deserializeArrayItem(string& value) LUX_OVERRIDE;
		
	private:
		void deserializeLabel(const char* label);

		inline void writeString(const char* str)
		{
			m_stream.write("\"", 1);
			m_stream.write(str);
			m_stream.write("\"", 1);
		}

		inline void writeBlockComma()
		{
			if(!m_is_first_in_block)
			{
				m_stream.write(",\n");
			}
		}

		inline void skipControl()
		{
			unsigned char c = m_buffer;
			while(c == ','|| c == ' ' || c == '\t' || c == '{' || c == '[' || c == '\n' || c == '\r' || c == ':' || c == ']' || c == '}')
			{
				if(!m_stream.read(&c, 1))
					return;
			}
			m_buffer = c;
		}

	private:
		AccessMode m_access_mode;
		unsigned char m_buffer;
		bool m_is_first_in_block;
		unsigned char* m_data;
		IStream& m_stream;
};


} // !namespace lux
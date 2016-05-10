#pragma once


#include "engine/lumix.h"


namespace Lumix
{


	class IAllocator;
	class Path;


	namespace FS
	{
		class IFile;
	}


	class LUMIX_ENGINE_API JsonSerializer
	{
		friend class ErrorProxy;
		public:
			enum AccessMode
			{
				READ,
				WRITE
			};

		public:
			JsonSerializer(FS::IFile& file, AccessMode access_mode, const Path& path, IAllocator& allocator);
			~JsonSerializer();

			// serialize
			void serialize(const char* label, uint32 value);
			void serialize(const char* label, float value);
			void serialize(const char* label, int32 value);
			void serialize(const char* label, const char* value);
			void serialize(const char* label, const Path& value);
			void serialize(const char* label, bool value);
			void beginObject();
			void beginObject(const char* label);
			void endObject();
			void beginArray(const char* label);
			void endArray();
			void serializeArrayItem(uint32 value);
			void serializeArrayItem(int32 value);
			void serializeArrayItem(int64 value);
			void serializeArrayItem(float value);
			void serializeArrayItem(bool value);
			void serializeArrayItem(const char* value);

			// deserialize
			void deserialize(const char* label, uint32& value, uint32 default_value);
			void deserialize(const char* label, float& value, float default_value);
			void deserialize(const char* label, int32& value, int32 default_value);
			void deserialize(const char* label, char* value, int max_length, const char* default_value);
			void deserialize(const char* label, Path& value, const Path& default_value);
			void deserialize(const char* label, bool& value, bool default_value);
			void deserialize(char* value, int max_length, const char* default_value);
			void deserialize(Path& path, const Path& default_value);
			void deserialize(bool& value, bool default_value);
			void deserialize(float& value, float default_value);
			void deserialize(int32& value, int32 default_value);
			void deserializeArrayBegin(const char* label);
			void deserializeArrayBegin();
			void deserializeArrayEnd();
			bool isArrayEnd();
			void deserializeArrayItem(uint32& value, uint32 default_value);
			void deserializeArrayItem(int32& value, int32 default_value);
			void deserializeArrayItem(int64& value, int64 default_value);
			void deserializeArrayItem(float& value, float default_value);
			void deserializeArrayItem(bool& value, bool default_value);
			void deserializeArrayItem(char* value, int max_length, const char* default_value);
			void deserializeObjectBegin();
			void deserializeObjectEnd();
			void deserializeLabel(char* label, int max_length);
			void deserializeRawString(char* buffer, int max_length);
			void nextArrayItem();
			bool isNextBoolean() const;
			bool isObjectEnd();

			bool isError() const { return m_is_error; }

		private:
			void deserializeLabel(const char* label);
			void deserializeToken();
			void deserializeArrayComma();
			float tokenToFloat();
			void expectToken(char expected_token);
			void writeString(const char* str);
			void writeBlockComma();

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
			char m_path[MAX_PATH_LENGTH];
			IAllocator& m_allocator;

			const char* m_data;
			int m_data_size;
			bool m_own_data;
			bool m_is_error;
	};


} // !namespace Lumix

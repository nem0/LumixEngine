#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct IAllocator;
struct IInputStream;
struct IOutputStream;
class Path;


class LUMIX_ENGINE_API JsonSerializer
{
	public:
		JsonSerializer(IOutputStream& file, const Path& path);
		void operator=(const JsonSerializer&) = delete;
		JsonSerializer(const JsonSerializer&) = delete;

		void serialize(const char* label, EntityPtr value);
		void serialize(const char* label, u32 value);
		void serialize(const char* label, u16 value);
		void serialize(const char* label, float value);
		void serialize(const char* label, double value);
		void serialize(const char* label, i32 value);
		void serialize(const char* label, const char* value);
		void serialize(const char* label, const Path& value);
		void serialize(const char* label, bool value);
		void beginObject();
		void beginObject(const char* label);
		void endObject();
		void beginArray();
		void beginArray(const char* label);
		void endArray();
		void serializeArrayItem(EntityPtr value);
		void serializeArrayItem(u32 value);
		void serializeArrayItem(i32 value);
		void serializeArrayItem(i64 value);
		void serializeArrayItem(float value);
		void serializeArrayItem(double value);
		void serializeArrayItem(bool value);
		void serializeArrayItem(const char* value);

	private:
		float tokenToFloat();
		float tokenToDouble();
		void writeString(const char* str);
		void writeBlockComma();

	private:
		bool m_is_first_in_block;
		IOutputStream& m_file;
};


class LUMIX_ENGINE_API JsonDeserializer
{
	friend class ErrorProxy;
public:
	JsonDeserializer(IInputStream& file, const Path& path, IAllocator& allocator);
	void operator=(const JsonDeserializer&) = delete;
	JsonDeserializer(const JsonDeserializer&) = delete;
	~JsonDeserializer();

	void deserialize(const char* label, EntityPtr& value, EntityPtr default_value);
	void deserialize(const char* label, u32& value, u32 default_value);
	void deserialize(const char* label, u16& value, u16 default_value);
	void deserialize(const char* label, float& value, float default_value);
	void deserialize(const char* label, double& value, double default_value);
	void deserialize(const char* label, i32& value, i32 default_value);
	void deserialize(const char* label, char* value, int max_length, const char* default_value);
	void deserialize(const char* label, Path& value, const Path& default_value);
	void deserialize(const char* label, bool& value, bool default_value);
	void deserialize(char* value, int max_length, const char* default_value);
	void deserialize(Path& path, const Path& default_value);
	void deserialize(bool& value, bool default_value);
	void deserialize(float& value, float default_value);
	void deserialize(double& value, double default_value);
	void deserialize(i32& value, i32 default_value);
	void deserializeArrayBegin(const char* label);
	void deserializeArrayBegin();
	void deserializeArrayEnd();
	bool isArrayEnd();
	void deserializeArrayItem(EntityPtr& value, EntityPtr default_value);
	void deserializeArrayItem(u32& value, u32 default_value);
	void deserializeArrayItem(i32& value, i32 default_value);
	void deserializeArrayItem(i64& value, i64 default_value);
	void deserializeArrayItem(float& value, float default_value);
	void deserializeArrayItem(double& value, double default_value);
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
	float tokenToDouble();
	void expectToken(char expected_token);

private:
	bool m_is_first_in_block;
	IInputStream& m_file;
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


} // namespace Lumix

#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct IAllocator;


LUMIX_ENGINE_API const char* stristr(const char* haystack, const char* needle);
LUMIX_ENGINE_API bool toCStringHex(u8 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(i32 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(u32 value, char* output, int length);
LUMIX_ENGINE_API bool toCStringPretty(u64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(i32 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(i64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(u64 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(u32 value, char* output, int length);
LUMIX_ENGINE_API bool toCString(float value, char* output, int length, int after_point);
LUMIX_ENGINE_API const char* reverseFind(const char* begin_haystack, const char* end_haystack, char c);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, i32* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, u64* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, i64* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, u32* value);
LUMIX_ENGINE_API const char* fromCString(const char* input, int length, u16* value);
LUMIX_ENGINE_API bool copyString(char* destination, int length, const char* source);
LUMIX_ENGINE_API bool copyNString(char* destination,
	int length,
	const char* source,
	int source_len);
LUMIX_ENGINE_API bool catString(char* destination, int length, const char* source);
LUMIX_ENGINE_API bool catNString(char* destination, int length, const char* source, int source_len);
LUMIX_ENGINE_API bool makeLowercase(char* destination, int length, const char* source);
LUMIX_ENGINE_API char makeUppercase(char c);
LUMIX_ENGINE_API bool makeUppercase(char* destination, int length, const char* source);
LUMIX_ENGINE_API char* trimmed(char* str);
LUMIX_ENGINE_API bool startsWith(const char* str, const char* prefix);
LUMIX_ENGINE_API int stringLength(const char* str);
LUMIX_ENGINE_API bool equalStrings(const char* lhs, const char* rhs);
LUMIX_ENGINE_API bool equalIStrings(const char* lhs, const char* rhs);
LUMIX_ENGINE_API int compareMemory(const void* lhs, const void* rhs, size_t size);
LUMIX_ENGINE_API int compareString(const char* lhs, const char* rhs);
LUMIX_ENGINE_API int compareStringN(const char* lhs, const char* rhs, int length);
LUMIX_ENGINE_API int compareIStringN(const char* lhs, const char* rhs, int length);
LUMIX_ENGINE_API void copyMemory(void* dest, const void* src, size_t count);
LUMIX_ENGINE_API void moveMemory(void* dest, const void* src, size_t count);
LUMIX_ENGINE_API void setMemory(void* ptr, u8 value, size_t num);
LUMIX_ENGINE_API const char* findSubstring(const char* str, const char* substr);
LUMIX_ENGINE_API bool endsWith(const char* str, const char* substr);


inline bool isLetter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}


inline bool isNumeric(char c)
{
	return c >= '0' && c <= '9';
}


inline bool isUpperCase(char c)
{
	return c >= 'A' && c <= 'Z';
}


template <int SIZE> bool copyString(char(&destination)[SIZE], const char* source)
{
	return copyString(destination, SIZE, source);
}

template <int SIZE> bool catString(char(&destination)[SIZE], const char* source)
{
	return catString(destination, SIZE, source);
}


template <int size> struct StaticString
{
	StaticString() { data[0] = '\0'; }

	explicit StaticString(const char* str) { copyString(data, size, str); }

	template <typename... Args> StaticString(const char* str, Args... args)
	{
		copyString(data, size, str);
		int tmp[] = { (add(args), 0)... };
		(void)tmp;
	}

	template <int value_size> StaticString& operator<<(StaticString<value_size>& value)
	{
		add(value);
		return *this;
	}

	template <typename T> StaticString& operator<<(T value)
	{
		add(value);
		return *this;
	}

	template <int value_size> void add(StaticString<value_size>& value) { catString(data, size, value.data); }
	void add(const char* value) { catString(data, size, value); }
	void add(char* value) { catString(data, size, value); }

	void operator=(const char* str) { copyString(data, str); }

	void add(float value)
	{
		int len = stringLength(data);
		toCString(value, data + len, size - len, 3);
	}

	template <typename T> void add(T value)
	{
		int len = stringLength(data);
		toCString(value, data + len, size - len);
	}

	bool operator<(const char* str) const {
		return compareString(data, str) < 0;
	}

	bool operator==(const char* str) const {
		return equalStrings(data, str);
	}

	bool operator!=(const char* str) const {
		return !equalStrings(data, str);
	}

	StaticString<size> operator +(const char* rhs)
	{
		return StaticString<size>(*this, rhs);
	}

	bool empty() const { return data[0] == '\0'; }

	operator const char*() const { return data; }
	char data[size];
};


struct StringView
{
	StringView() : begin(nullptr), end(nullptr) {}
	
	StringView(const char* begin)
		: begin(begin)
		, end(begin + stringLength(begin))
	{
	}

	StringView(const char* begin, int len)
		: begin(begin)
		, end(begin + len)
	{
	}

	size_t length() const { return end - begin; }

	const char* begin;
	const char* end;
};


class LUMIX_ENGINE_API string
{
public:
	explicit string(IAllocator& allocator);
	string(const string& rhs, int start, i32 length);
	string(const char* rhs, i32 length, IAllocator& allocator);
	string(const string& rhs);
	string(const char* rhs, IAllocator& allocator);
	~string();

	void resize(int size);
	char* getData() { return m_cstr; }
	char operator[](int index) const;
	void set(const char* rhs, int size);
	void operator=(const string& rhs);
	void operator=(const char* rhs);
	bool operator!=(const string& rhs) const;
	bool operator!=(const char* rhs) const;
	bool operator==(const string& rhs) const;
	bool operator==(const char* rhs) const;
	bool operator<(const string& rhs) const;
	bool operator>(const string& rhs) const;
	int length() const { return m_size; }
	const char* c_str() const { return m_cstr; }
	string substr(int start, int length) const;
	string& cat(const char* value, int length);
	string& cat(float value);
	string& cat(char* value);
	string& cat(const char* value);
	void insert(int position, const char* value);
	void eraseAt(int position);

	template <class V> string& cat(V value)
	{
		char tmp[30];
		toCString(value, tmp, 30);
		cat(tmp);
		return *this;
	}

	IAllocator& m_allocator;
private:
	i32 m_size;
	char* m_cstr;
};



} // namespace Lumix

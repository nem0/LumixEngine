#pragma once

#include "engine/lumix.h"
#include "engine/hash.h"

namespace Lumix {

struct IAllocator;
template <int SIZE> struct StaticString;

struct StringView {
	StringView() {}
	StringView(const char* str) : begin(str) {}
	StringView(const char* str, u32 len) : begin(str), end(str + len) {}
	StringView(const char* begin, const char* end) : begin(begin), end(end) {}
	template <int N> StringView(const StaticString<N>& str);

	u32 size() const { ASSERT(end); return u32(end - begin); }
	char operator[](u32 idx) { ASSERT(!end || begin + idx < end); return begin[idx]; }
	void ensureEnd();
	char back() const { ASSERT(end && begin != end); return *(end - 1); }
	void removeSuffix(u32 count) { ensureEnd(); ASSERT(count <= size()); end -= count; }
	void removePrefix(u32 count) { ensureEnd(); ASSERT(count <= size()); begin += count; }
	bool empty() const { return begin == end || !begin[0]; }

	const char* begin = nullptr;
	const char* end = nullptr; // if end is null and begin is not null, begin is null-terminated
};

LUMIX_ENGINE_API const char* stristr(StringView haystack, StringView needle);
LUMIX_ENGINE_API bool contains(StringView haystack, char needle);
LUMIX_ENGINE_API bool toCStringHex(u8 value, Span<char> output);
LUMIX_ENGINE_API bool toCStringPretty(i32 value, Span<char> output);
LUMIX_ENGINE_API bool toCStringPretty(u32 value, Span<char> output);
LUMIX_ENGINE_API bool toCStringPretty(u64 value, Span<char> output);
LUMIX_ENGINE_API bool toCString(bool value, Span<char> output);
LUMIX_ENGINE_API bool toCString(i32 value, Span<char> output);
inline bool toCString(EntityPtr value, Span<char> output) { return toCString(value.index, output); }
LUMIX_ENGINE_API bool toCString(i64 value, Span<char> output);
LUMIX_ENGINE_API bool toCString(u64 value, Span<char> output);
LUMIX_ENGINE_API bool toCString(u32 value, Span<char> output);
LUMIX_ENGINE_API bool toCString(float value, Span<char> output, int after_point);
LUMIX_ENGINE_API bool toCString(double value, Span<char> output, int after_point);
LUMIX_ENGINE_API const char* reverseFind(StringView haystack, char c);
LUMIX_ENGINE_API const char* fromCStringOctal(StringView input, u32& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, i32& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, u64& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, i64& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, u32& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, u16& value);
LUMIX_ENGINE_API const char* fromCString(StringView input, bool& value);
inline const char* fromCString(StringView input, EntityPtr& value) { return fromCString(input, value.index); };
LUMIX_ENGINE_API bool copyString(Span<char> output, StringView source);
LUMIX_ENGINE_API bool catString(Span<char> output, StringView source);
LUMIX_ENGINE_API bool makeLowercase(Span<char> output, StringView source);
LUMIX_ENGINE_API bool startsWith(StringView str, StringView prefix);
LUMIX_ENGINE_API bool startsWithInsensitive(StringView str, StringView prefix);
LUMIX_ENGINE_API int stringLength(const char* str);
LUMIX_ENGINE_API bool equalStrings(StringView lhs, StringView rhs);
LUMIX_ENGINE_API bool equalIStrings(StringView lhs, StringView rhs);
LUMIX_ENGINE_API int compareString(StringView lhs, StringView rhs);
LUMIX_ENGINE_API const char* findChar(StringView str, char needle);
LUMIX_ENGINE_API bool endsWith(StringView str, StringView suffix);
LUMIX_ENGINE_API bool endsWithInsensitive(StringView str, StringView suffix);
LUMIX_ENGINE_API bool isLetter(char c);
LUMIX_ENGINE_API bool isNumeric(char c);
LUMIX_ENGINE_API bool isUpperCase(char c);

template <int SIZE> bool copyString(char(&destination)[SIZE], StringView source) {
	return copyString(Span<char>(destination, SIZE), source);
}

template <int SIZE> bool catString(char(&destination)[SIZE], StringView source) {
	return catString(Span<char>(destination, SIZE), source);
}

template <int SIZE> struct StaticString {
	StaticString() { data[0] = '\0'; }

	explicit StaticString(const char* str) { copyString(Span(data), str); }

	template <typename... Args> StaticString(Args... args) {
		data[0] = '\0';
		int tmp[] = { (add(args), 0)... };
		(void)tmp;
	}

	template <typename... Args> void append(Args... args) {
		int tmp[] = { (add(args), 0)... };
		(void)tmp;
	}

	template <int value_size> void add(StaticString<value_size>& value) { catString(data, value.data); }
	void add(const char* value) { catString(data, value); }
	void add(char* value) { catString(data, value); }
	void add(StringView value) { catString(Span(data), value); }
	void add(StableHash value) { add(value.getHashValue()); }

	void add(char value) {
		char tmp[2] = {value, 0};
		catString(data, tmp);
	}

	void add(float value) {
		int len = stringLength(data);
		toCString(value, Span<char>(data).fromLeft(len), 3);
	}

	void add(double value) {
		int len = stringLength(data);
		toCString(value, Span<char>(data).fromLeft(len), 10);
	}

	template <typename T> void add(T value) {
		int len = stringLength(data);
		toCString(value, Span(data + len, u32(SIZE - len)));
	}

	void operator=(const char* str) { copyString(data, str); }
	bool operator<(const char* str) const { return compareString(data, str) < 0; }
	bool operator==(const char* str) const { return equalStrings(data, str); }
	bool operator!=(const char* str) const { return !equalStrings(data, str); }

	bool empty() const { return data[0] == '\0'; }
	operator const char*() const { return data; }

	char data[SIZE];
};


struct LUMIX_ENGINE_API String {
	explicit String(IAllocator& allocator);
	String(const String& rhs, u32 start, u32 length);
	String(StringView rhs, IAllocator& allocator);
	String(const String& rhs);
	String(String&& rhs);
	~String();

	void resize(u32 size);
	char* getData() { return isSmall() ? m_small : m_big; }
	char operator[](u32 index) const;
	void operator=(const String& rhs);
	void operator=(StringView rhs);
	void operator=(String&& rhs);
	bool operator!=(const String& rhs) const;
	bool operator!=(const char* rhs) const;
	bool operator==(const String& rhs) const;
	bool operator==(const char* rhs) const;
	bool operator<(const String& rhs) const;
	bool operator>(const String& rhs) const;
	operator StringView() const { return StringView{c_str(), m_size}; }
	int length() const { return m_size; }
	const char* c_str() const { return isSmall() ? m_small : m_big; }
	String substr(u32 start, u32 length) const;
	String& cat(StringView value);
	String& cat(float value);
	String& cat(char* value);
	String& cat(const char* value);
	void insert(u32 position, const char* value);
	void eraseAt(u32 position);

	template <typename V> String& cat(V value)
	{
		char tmp[64];
		toCString(value, Span<char>(tmp));
		cat(tmp);
		return *this;
	}

	IAllocator& m_allocator;
private:
	bool isSmall() const { return m_size < sizeof(m_small); }
	void ensure(u32 size);

	u32 m_size;
	union {
		char* m_big;
		char m_small[16];
	};
};

template <int N> StringView::StringView(const StaticString<N>& str) : begin(str.data) {}

} // namespace Lumix

#include "string.h"
#include "engine/allocator.h"
#include "engine/crt.h"


namespace Lumix
{

static char toLower(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
	return c;
}

bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isNumeric(char c) { return c >= '0' && c <= '9'; }
bool isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }

String::String(IAllocator& allocator)
	: m_allocator(allocator)
{
	m_small[0] = '\0';
	m_size = 0;
}


String::String(const String& rhs, u32 start, u32 length)
	: m_allocator(rhs.m_allocator)
{
	m_size = 0;
	*this = StringView(rhs.c_str() + start, length);
}



String::String(StringView rhs, IAllocator& allocator)
	: m_allocator(allocator)
{
	m_size = 0;
	*this = rhs;
}


String::String(const String& rhs)
	: m_allocator(rhs.m_allocator)
{
	m_size = 0;
	*this = rhs;
}


String::String(String&& rhs)
	: m_allocator(rhs.m_allocator)
{
	if (rhs.isSmall()) {
		memcpy(m_small, rhs.m_small, sizeof(m_small));
		rhs.m_small[0] = '\0';
	}
	else {
		m_big = rhs.m_big;
		rhs.m_big = nullptr;
	}
	m_size = rhs.m_size;
	rhs.m_size = 0;
}

void String::operator=(String&& rhs)
{
	if (&rhs == this) return;

	if (!isSmall()) {
		m_allocator.deallocate(m_big);
	}

	if (rhs.isSmall()) {
		memcpy(m_small, rhs.m_small, sizeof(m_small));
		rhs.m_small[0] = '\0';
	}
	else {
		m_big = rhs.m_big;
		rhs.m_big = nullptr;
	}
	m_size = rhs.m_size;
	rhs.m_size = 0;
}


String::~String() { if (!isSmall()) m_allocator.deallocate(m_big); }


char String::operator[](u32 index) const
{
	ASSERT(index < m_size);
	return isSmall() ? m_small[index] : m_big[index];
}


void String::operator=(const String& rhs)
{
	if (&rhs == this) return;

	*this = StringView{rhs};
}

void String::operator=(StringView rhs) {
	if (!isSmall()) {
		ASSERT(rhs.begin > m_big + m_size || rhs.end < m_big);
		m_allocator.deallocate(m_big);
	}
		
	if (rhs.size() < sizeof(m_small)) {
		ASSERT(rhs.begin > m_small + m_size || rhs.end < m_small);
		memcpy(m_small, rhs.begin, rhs.size());
		m_small[rhs.size()] = '\0';
	}
	else {
		m_big = (char*)m_allocator.allocate(rhs.size() + 1);
		memcpy(m_big, rhs.begin, rhs.size());
		m_big[rhs.size()] = '\0';
	}
	m_size = rhs.size();
}


bool String::operator!=(StringView rhs) const
{
	return !equalStrings(*this, rhs);
}


bool String::operator==(StringView rhs) const
{
	return equalStrings(*this, rhs);
}


void String::resize(u32 size) {
	if (isSmall()) {
		if (size < sizeof(m_small)) {
			m_small[size] = '\0';
		}
		else {
			char* tmp = (char*)m_allocator.allocate(size + 1);
			memcpy(tmp, m_small, m_size + 1);
			tmp[size] = '\0';
			m_big = tmp;
		}
	}
	else {
		if (size < sizeof(m_small)) {
			char* tmp = m_big;
			memcpy(m_small, tmp, m_size >= sizeof(m_small) ? sizeof(m_small) - 1 : m_size + 1);
			m_small[size] = '\0';
			m_allocator.deallocate(tmp);
		}
		else {
			m_big = (char*)m_allocator.reallocate(m_big, size + 1, m_size + 1);
			m_big[size] = '\0';
		}
	}
	m_size = size;
}


String& String::cat(StringView value) {
	ASSERT(value.begin < c_str() || value.begin >= c_str() + m_size);

	const int old_s = m_size;
	resize(m_size + value.size());
	memcpy(getData() + old_s, value.begin, value.size());
	getData()[old_s + value.size()] = '\0';
	return *this;
}


void String::eraseAt(u32 position)
{
	ASSERT(position < m_size);
	if (position >= m_size) return;
	
	memmove(getData() + position, getData() + position + 1, m_size - position - 1);
	--m_size;
	getData()[m_size] = '\0';
}


void String::insert(u32 position, const char* value)
{
	ASSERT(value < c_str() || value > c_str() + m_size);
	
	const int old_size = m_size;
	const int len = stringLength(value);
	resize(old_size + len);

	char* tmp = getData();
	memmove(tmp + position + len, tmp + position, old_size - position);
	memcpy(tmp + position, value, len);
}


static char makeLowercase(char c) {
	return c >= 'A' && c <= 'Z' ? c - ('A' - 'a') : c;
}

int compareString(StringView lhs, StringView rhs) {
	const char* a = lhs.begin;
	const char* b = rhs.begin;

	while (a != lhs.end && b != lhs.end && *a == *b) {
		++a;
		++b;
	}

	i32 ac = a == lhs.end ? 0 : *a;
	i32 bc = b == rhs.end ? 0 : *b;

	return ac - bc;
}

bool equalStrings(StringView lhs, StringView rhs) {
	if (rhs.size() != lhs.size()) return false;
	return strncmp(lhs.begin, rhs.begin, lhs.size()) == 0;
}

bool equalIStrings(StringView lhs, StringView rhs) {
	if (lhs.size() != rhs.size()) return false;

	for (u32 i = 0, c = lhs.size(); i < c; ++i) {
		if (toLower(lhs[i]) != toLower(rhs[i])) return false;
	}
	return true;
}

int stringLength(const char* str)
{
	return (int)strlen(str);
}

bool endsWithInsensitive(StringView str, StringView suffix) {
	if (str.size() > suffix.size()) return false;
	str.begin = str.end - suffix.size();
	return equalIStrings(str, suffix);
}

bool endsWith(StringView str, StringView suffix) {
	if (str.size() > suffix.size()) return false;
	str.begin = str.end - suffix.size();
	return equalStrings(str, suffix);
}

const char* stristr(StringView haystack, StringView needle) {
	ASSERT(!needle.empty());

	const char* c = haystack.begin;
	while (c != haystack.end) {
		if (makeLowercase(*c) == makeLowercase(needle[0])) {
			const char* n = needle.begin + 1;
			const char* c2 = c + 1;
			while (n != needle.end && c2 != haystack.end) {
				if (makeLowercase(*n) != makeLowercase(*c2)) break;
				++n;
				++c2;
			}
			if (n == needle.end) return c;
		}
		++c;
	}
	return nullptr;
}

bool makeLowercase(Span<char> output, StringView src) {
	char* destination = output.begin();
	if (src.size() + 1 > output.length()) return false;

	const char* source = src.begin;
	while (source != src.end) {
		*destination = makeLowercase(*source);
		++destination;
		++source;
	}
	*destination = 0;
	return true;
}

const char* findChar(StringView haystack, char needle) {
	const char* c = haystack.begin;
	while (c != haystack.end) {
		if (*c == needle) return c;
		++c;
	}
	return nullptr;
}

bool contains(StringView haystack, char needle) {
	return findChar(haystack, needle) != nullptr;
}

char* copyString(Span<char> dst, StringView src) {
	if (dst.length() < 1) return dst.begin();

	ASSERT(dst.begin() >= src.end || dst.begin() <= src.begin);

	u32 length = dst.length();
	char* tmp = dst.begin();
	const char* srcp = src.begin;
	while (srcp != src.end && length > 1) {
		*tmp = *srcp;
		--length;
		++tmp;
		++srcp;
	}
	*tmp = 0;
	return tmp;
}

const char* reverseFind(StringView haystack, char c) {
	if (haystack.size() == 0) return nullptr;

	const char* tmp = haystack.end - 1;
	while (tmp >= haystack.begin) {
		if (*tmp == c) return tmp;
		--tmp;
	}
	return nullptr;
}

char* catString(Span<char> destination, StringView source) {
	char* dst = destination.begin();
	u32 length = destination.length();
	while (*dst && length) {
		--length;
		++dst;
	}
	return copyString(Span(dst, length), source);
}

static void reverse(char* str, int length)
{
	char* beg = str;
	char* end = str + length - 1;
	while (beg < end)
	{
		char tmp = *beg;
		*beg = *end;
		*end = tmp;
		++beg;
		--end;
	}
}

const char* fromCString(StringView input, i32& value) {
	i64 val;
	const char* ret = fromCString(input, val);
	value = (i32)val;
	return ret;
}

const char* fromCString(StringView input, i64& value) {
	if (input.empty()) return nullptr;

	const char* c = input.begin;
	if (*c == '-') ++c;

	u64 tmp;
	const char* res = fromCString(StringView(c, input.end), tmp);
	if (!res) return nullptr;

	value = input[0] == '-' ? -i64(tmp) : i64(tmp);

	return res;
}

const char* fromCString(StringView input, u16& value) {
	u32 tmp;
	const char* ret = fromCString(input, tmp);
	value = u16(tmp);
	return ret;
}

const char* fromCString(StringView input, u32& value) {
	u64 tmp;
	const char* ret = fromCString(input, tmp);
	value = u32(tmp);
	return ret;
}

const char* fromCStringOctal(StringView input, u32& value) {
	if (input.empty()) return nullptr;

	const char* c = input.begin;
	value = 0;
	if (*c < '0' || *c > '7') return nullptr;

	while (c != input.end && *c >= '0' && *c <= '7') {
		value *= 8;
		value += *c - '0';
		++c;
	}
	return c;
}

const char* fromCString(StringView input, u64& value) {
	if (input.empty()) return nullptr;
	
	const char* c = input.begin;
	value = 0;
	if (*c < '0' || *c > '9') return nullptr;

	while (c != input.end && *c >= '0' && *c <= '9') {
		value *= 10;
		value += *c - '0';
		++c;
	}
	return c;
}

void toCStringPretty(i32 value, Span<char> output)
{
	char* c = output.begin();
	u32 length = output.length();
	if (length > 0)
	{
		if (value < 0)
		{
			value = -value;
			--length;
			*c = '-';
			++c;
		}
		return toCStringPretty((unsigned int)value, Span(c, length));
	}
}


void toCStringPretty(u32 value, Span<char> output)
{
	return toCStringPretty(u64(value), output);
}


void toCStringPretty(u64 value, Span<char> output)
{
	char* c = output.begin();
	char* num_start = output.begin();
	u32 length = output.length();
	if (length > 0)
	{
		if (value == 0)
		{
			if (length == 1)
			{
				return;
			}
			*c = '0';
			*(c + 1) = 0;
			return;
		}
		int counter = 0;
		while (value > 0 && length > 1)
		{
			*c = value % 10 + '0';
			value = value / 10;
			--length;
			++c;
			if ((counter + 1) % 3 == 0 && length > 1 && value > 0)
			{
				*c = ' ';
				++c;
				counter = 0;
			}
			else
			{
				++counter;
			}
		}
		if (length > 0)
		{
			reverse(num_start, (int)(c - num_start));
			*c = 0;
			return;
		}
	}
	return;
}


char* toCString(i32 value, Span<char> output)
{
	char* c = output.begin();
	u32 length = output.length();
	if (length < 2) return nullptr;

	if (value < 0) {
		value = -value;
		--length;
		c[0] = '-';
		c[1] = 0;
		++c;
	}
	return toCString((u32)value, Span(c, length));
}

char* toCString(i64 value, Span<char> output)
{
	char* c = output.begin();
	u32 length = output.length();
	if (length < 2) return nullptr;
	if (value < 0) {
		value = -value;
		--length;
		c[0] = '-';
		c[1] = 0;
		++c;
	}
	return toCString((u64)value, Span(c, length));
}

char* toCString(u64 value, Span<char> output)
{
	char* c = output.begin();
	char* num_start = output.begin();
	u32 length = output.length();
	if (length < 2) return nullptr;

	if (value == 0) {
		*c = '0';
		*(c + 1) = 0;
		return c + 1;
	}

	while (value > 0 && length > 0) {
		*c = value % 10 + '0';
		value = value / 10;
		--length;
		++c;
	}

	if (length > 0) {
		reverse(num_start, (int)(c - num_start));
		*c = 0;
		return c;
	}

	*(c - 1) = 0;
	return nullptr;
}

void toCStringHex(u8 value, Span<char> output)
{
	if (output.length() < 2)
	{
		return;
	}
	u8 first = value / 16;
	if (first > 9)
	{
		output[0] = 'A' + first - 10;
	}
	else
	{
		output[0] = '0' + first;
	}
	u8 second = value % 16;
	if (second > 9)
	{
		output[1] = 'A' + second - 10;
	}
	else
	{
		output[1] = '0' + second;
	}
	return;
}

char* toCString(u32 value, Span<char> output)
{
	char* c = output.begin();
	char* num_start = output.begin();
	u32 length = output.length();
	if (length < 2) return nullptr;

	if (value == 0) {
		*c = '0';
		*(c + 1) = 0;
		return c + 1;
	}

	while (value > 0 && length > 0) {
		*c = value % 10 + '0';
		value = value / 10;
		--length;
		++c;
	}
	if (length > 0) {
		reverse(num_start, (int)(c - num_start));
		*c = 0;
		return c;
	}
	*(c- 1) = 0;
	return nullptr;
}

// returns new end or null if failed
static char* increment(const char* output, char* end, bool is_space_after)
{
	char carry = 1;
	{
		char* c = end - 1; // skip '\0'
		while (c >= output)
		{
			if (*c == '.')
			{
				--c;
			}
			*c += carry;
			if (*c > '9')
			{
				*c = '0';
				carry = 1;
			}
			else
			{
				carry = 0;
				break;
			}
			--c;
		}
	}
	if (carry && is_space_after) {
		char* c = end; // including '\0' at the end of the String
		while (c >= output) {
			*(c + 1) = *c;
			--c;
		}
		++c;
		*c = '1';
		return end + 1;
	}
	if (carry) return nullptr;
	return end;
}


char* toCString(float value, Span<char> out, int after_point) {
	return toCString(double(value), out, after_point);
}

char* toCString(bool value, Span<char> output) { return copyString(output, value ? "true" : "false"); }

char* toCString(double value, Span<char> out, int after_point) {
	char* output = out.begin();
	u32 length = out.length();
	if (length < 2) return nullptr;
	
	if (value < 0) {
		*output = '-';
		++output;
		value = -value;
		--length;
	}
	// int part
	int exponent = value == 0 ? 0 : (int)log10(value);
	double num = value;
	char* c = output;
	if (num < 1 && length > 1) {
		*c = '0';
		++c;
		--length;
	}
	else {
		while ((num >= 1 || exponent >= 0) && length > 1) {
			const double power = pow(10.0, (double)exponent);
			char digit = (char)floor(num / power);
			num -= digit * power;
			*c = digit + '0';
			--exponent;
			--length;
			++c;
		}
	}
	// decimal part
	double dec_part = num;
	if (length > 1 && after_point > 0) {
		*c = '.';
		++c;
		--length;
	}
	else if (length > 0 && after_point == 0) {
		*c = 0;
		return c;
	}
	else {
		return nullptr;
	}
	while (length > 1 && after_point > 0) {
		dec_part *= 10;
		char tmp = (char)dec_part;
		*c = tmp + '0';
		dec_part -= tmp;
		++c;
		--length;
		--after_point;
	}
	*c = 0;
	if ((int)(dec_part + 0.5f))
		c = increment(output, c, length > 1);
	return c;
}

bool startsWith(StringView str, StringView prefix) {
	if (str.size() < prefix.size()) return false;
	str.end = str.begin + prefix.size();
	return equalStrings(str, prefix);
}

bool startsWithInsensitive(StringView str, StringView prefix) {
	if (str.size() < prefix.size()) return false;
	str.end = str.begin + prefix.size();
	return equalIStrings(str, prefix);
}

} // namespace Lumix

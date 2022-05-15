#include "string.h"
#include "engine/allocator.h"
#include "engine/crt.h"


namespace Lumix
{
	
bool isLetter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}


bool isNumeric(char c)
{
	return c >= '0' && c <= '9';
}


bool isUpperCase(char c)
{
	return c >= 'A' && c <= 'Z';
}


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
	*this = Span(rhs.c_str() + start, length);
}



String::String(Span<const char> rhs, IAllocator& allocator)
	: m_allocator(allocator)
{
	m_size = 0;
	*this = rhs;
}


String::String(const String& rhs)
	: m_allocator(rhs.m_allocator)
{
	m_size = 0;
	*this = Span(rhs.c_str(), rhs.length());
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


String::String(const char* rhs, IAllocator& allocator)
	: m_allocator(allocator)
{
	m_size = stringLength(rhs);
	if (isSmall()) {
		memcpy(m_small, rhs, m_size + 1);
	}
	else {
		m_big = (char*)m_allocator.allocate(m_size + 1);
		memcpy(m_big, rhs, m_size + 1);
	}
}


String::~String() { if (!isSmall()) m_allocator.deallocate(m_big); }


char String::operator[](u32 index) const
{
	ASSERT(index >= 0 && index < m_size);
	return isSmall() ? m_small[index] : m_big[index];
}


void String::operator=(const String& rhs)
{
	if (&rhs == this) return;

	*this = Span(rhs.c_str(), rhs.length());
}

void String::operator=(Span<const char> rhs)
{
	if (!isSmall()) {
		m_allocator.deallocate(m_big);
	}
		
	if (rhs.length() < sizeof(m_small)) {
		memcpy(m_small, rhs.m_begin, rhs.length());
		m_small[rhs.length()] = '\0';
	}
	else {
		m_big = (char*)m_allocator.allocate(rhs.length() + 1);
		memcpy(m_big, rhs.m_begin, rhs.length());
		m_big[rhs.length()] = '\0';
	}
	m_size = rhs.length();
}


void String::operator=(const char* rhs)
{
	*this = Span(rhs, stringLength(rhs));
}


bool String::operator!=(const String& rhs) const
{
	return compareString(c_str(), rhs.c_str()) != 0;
}


bool String::operator!=(const char* rhs) const
{
	return compareString(c_str(), rhs) != 0;
}


bool String::operator==(const String& rhs) const
{
	return compareString(c_str(), rhs.c_str()) == 0;
}


bool String::operator==(const char* rhs) const
{
	return compareString(c_str(), rhs) == 0;
}


bool String::operator<(const String& rhs) const
{
	return compareString(c_str(), rhs.c_str()) < 0;
}


bool String::operator>(const String& rhs) const
{
	return compareString(c_str(), rhs.c_str()) > 0;
}


String String::substr(u32 start, u32 length) const
{
	return String(*this, start, length);
}


void String::resize(u32 size)
{
	ASSERT(size > 0);
	if (size == 0) return;
	
	if (isSmall()) {
		if (size < sizeof(m_small)) {
			m_size = size;
			m_small[size] = '\0';
		}
		else {
			char* tmp = (char*)m_allocator.allocate(size + 1);
			memcpy(tmp, m_small, m_size + 1);
			m_size = size;
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
			m_big = (char*)m_allocator.reallocate(m_big, size + 1);
			m_size = size;
			m_big[size] = '\0';
		}
	}
}


String& String::cat(Span<const char> value)
{
	ASSERT(value.begin() < c_str() || value.begin() >= c_str() + m_size);

	const int old_s = m_size;
	resize(m_size + value.length());
	memcpy(getData() + old_s, value.m_begin, value.length());
	getData()[old_s + value.length()] = '\0';
	return *this;
}


String& String::cat(float value)
{
	char tmp[40];
	toCString(value, Span(tmp), 10);
	cat(tmp);
	return *this;
}


String& String::cat(char* value)
{
	cat((const char*)value);
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


String& String::cat(const char* rhs)
{
	ASSERT(rhs < c_str() || rhs >= c_str() + m_size);
	
	const int len = stringLength(rhs);
	if(len == 0) return *this;
	const int old_s = m_size;
	resize(len + old_s);
	memcpy(getData() + old_s, rhs, len + 1);
	return *this;
}


static char makeLowercase(char c)
{
	return c >= 'A' && c <= 'Z' ? c - ('A' - 'a') : c;
}


int compareMemory(const void* lhs, const void* rhs, size_t size)
{
	return memcmp(lhs, rhs, size);
}


int compareStringN(const char* lhs, const char* rhs, int length)
{
	return strncmp(lhs, rhs, length);
}


int compareIStringN(const char* lhs, const char* rhs, int length)
{
#ifdef _WIN32
	return _strnicmp(lhs, rhs, length);
#else
	return strncasecmp(lhs, rhs, length);
#endif
}


int compareString(const char* lhs, const char* rhs)
{
	return strcmp(lhs, rhs);
}


bool equalStrings(const char* lhs, const char* rhs)
{
	return strcmp(lhs, rhs) == 0;
}

bool equalStrings(Span<const char> lhs, Span<const char> rhs)
{
	if (rhs.length() != lhs.length()) return false;
	return strncmp(lhs.begin(), rhs.begin(), lhs.length()) == 0;
}


bool equalIStrings(const char* lhs, const char* rhs)
{
#ifdef _WIN32
	return _stricmp(lhs, rhs) == 0;
#else
	return strcasecmp(lhs, rhs) == 0;
#endif
}

bool equalIStrings(Span<const char> lhs, const char* rhs)
{
#ifdef _WIN32
	return _strnicmp(lhs.begin(), rhs, lhs.length()) == 0 && strlen(rhs) == lhs.length();
#else
	return strncasecmp(lhs.begin(), rhs, lhs.length()) == 0 && strlen(rhs) == lhs.length();
#endif
}


int stringLength(const char* str)
{
	return (int)strlen(str);
}

bool endsWithInsensitive(const char* str, const char* substr)
{
	int len = stringLength(str);
	int len2 = stringLength(substr);
	if (len2 > len) return false;
	return equalIStrings(str + len - len2, substr);
}
bool endsWith(const char* str, const char* substr)
{
	int len = stringLength(str);
	int len2 = stringLength(substr);
	if (len2 > len) return false;
	return equalStrings(str + len - len2, substr);
}

bool contains(const char* haystack, char needle)
{
	const char* c = haystack;
	while (*c) {
		if (*c == needle) return true;
		++c;
	}
	return false;
}

const char* stristr(const char* haystack, const char* needle)
{
	const char* c = haystack;
	while (*c)
	{
		if (makeLowercase(*c) == makeLowercase(needle[0]))
		{
			const char* n = needle + 1;
			const char* c2 = c + 1;
			while (*n && *c2)
			{
				if (makeLowercase(*n) != makeLowercase(*c2)) break;
				++n;
				++c2;
			}
			if (*n == 0) return c;
		}
		++c;
	}
	return nullptr;
}

bool makeLowercase(Span<char> output, Span<const char> src) {
	char* destination = output.begin();
	if (src.length() + 1 > output.length()) return false;

	const char* source = src.begin();
	while (source != src.end())
	{
		*destination = makeLowercase(*source);
		++destination;
		++source;
	}
	*destination = 0;
	return true;
}

bool makeLowercase(Span<char> dst, const char* source)
{
	char* destination = dst.begin();
	u32 length = dst.length();
	if (!source)
	{
		return false;
	}

	while (*source && length)
	{
		*destination = makeLowercase(*source);
		--length;
		++destination;
		++source;
	}
	if (length > 0)
	{
		*destination = 0;
		return true;
	}
	return false;
}


const char* findSubstring(const char* haystack, const char* needle)
{
	return strstr(haystack, needle);
}


bool copyNString(Span<char> dst, const char* src, int N)
{
	if (!src) return false;

	char* destination = dst.begin();
	const char* source = src;
	u32 length = dst.length();
	ASSERT(N >= 0);

	while (*source && length > 1 && N)
	{
		*destination = *source;
		--N;
		--length;
		++destination;
		++source;
	}
	if (length > 0)
	{
		*destination = 0;
		return *source == '\0' || N == 0;
	}
	return false;
}

bool copyString(Span<char> dst, Span<const char> src)
{
	if (dst.length() < 1) return false;
	if (src.length() < 1) {
		*dst.m_begin = 0;
		return true;
	}

	u32 length = dst.length();
	char* tmp = dst.begin();
	const char* srcp = src.m_begin;
	while (srcp != src.m_end && length > 1) {
		*tmp = *srcp;
		--length;
		++tmp;
		++srcp;
	}
	*tmp = 0;
	return srcp == src.m_end;
}

bool copyString(Span<char> dst, const char* src)
{
	if (!src || dst.length() < 1) return false;

	u32 length = dst.length();
	char* tmp = dst.begin();
	while (*src && length > 1) {
		*tmp = *src;
		--length;
		++tmp;
		++src;
	}
	*tmp = 0;
	return *src == '\0';
}


const char* reverseFind(const char* begin_haystack, const char* end_haystack, char c)
{
	const char* tmp = end_haystack == nullptr ? nullptr : end_haystack - 1;
	if (tmp == nullptr)
	{
		tmp = begin_haystack;
		while (*tmp)
		{
			++tmp;
		}
	}
	while (tmp >= begin_haystack && *tmp != c)
	{
		--tmp;
	}
	if (tmp >= begin_haystack)
	{
		return tmp;
	}
	return nullptr;
}


bool catNString(Span<char> dst, const char* src, int N)
{
	char* destination = dst.begin();
	u32 length = dst.length();
	while (*destination && length) {
		--length;
		++destination;
	}
	return copyNString(Span(destination, length), src, N);
}


bool catString(Span<char> destination, const char* source)
{
	char* dst = destination.begin();
	u32 length = destination.length();
	while (*dst && length)
	{
		--length;
		++dst;
	}
	return copyString(Span(dst, length), source);
}

bool catString(Span<char> destination, Span<const char> source)
{
	char* dst = destination.begin();
	u32 length = destination.length();
	while (*dst && length)
	{
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

const char* fromCString(Span<const char> input, i32& value)
{
	i64 val;
	const char* ret = fromCString(input, val);
	value = (i32)val;
	return ret;
}

const char* fromCString(Span<const char> input, i64& value)
{
	u32 length = input.length();
	if (length > 0)
	{
		const char* c = input.begin();
		value = 0;
		if (*c == '-')
		{
			++c;
			--length;
			if (!length)
			{
				return nullptr;
			}
		}
		while (length && *c >= '0' && *c <= '9')
		{
			value *= 10;
			value += *c - '0';
			++c;
			--length;
		}
		if (input[0] == '-')
		{
			value = -value;
		}
		return c;
	}
	return nullptr;
}

const char* fromCString(Span<const char> input, u16& value)
{
	u32 tmp;
	const char* ret = fromCString(input, tmp);
	value = u16(tmp);
	return ret;
}

const char* fromCString(Span<const char> input, u32& value)
{
	u32 length = input.length();
	if (length > 0)
	{
		const char* c = input.begin();
		value = 0;
		if (*c == '-')
		{
			return nullptr;
		}
		while (length && *c >= '0' && *c <= '9')
		{
			value *= 10;
			value += *c - '0';
			++c;
			--length;
		}
		return c;
	}
	return nullptr;
}

const char* fromCStringOctal(Span<const char> input, u32& value)
{
	u32 length = input.length();
	if (length > 0)
	{
		const char* c = input.begin();
		value = 0;
		if (*c == '-') {
			return nullptr;
		}
		while (length && *c >= '0' && *c <= '7') {
			value *= 8;
			value += *c - '0';
			++c;
			--length;
		}
		return c;
	}
	return nullptr;
}

const char* fromCString(Span<const char> input, u64& value)
{
	u32 length = input.length();
	if (length > 0)
	{
		const char* c = input.begin();
		value = 0;
		if (*c == '-')
		{
			return nullptr;
		}
		while (length && *c >= '0' && *c <= '9')
		{
			value *= 10;
			value += *c - '0';
			++c;
			--length;
		}
		return c;
	}
	return nullptr;
}

const char* fromCString(Span<const char> input, bool& value)
{
	value = equalIStrings(input, "true");
	return input.end();
}

bool toCStringPretty(i32 value, Span<char> output)
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
	return false;
}


bool toCStringPretty(u32 value, Span<char> output)
{
	return toCStringPretty(u64(value), output);
}


bool toCStringPretty(u64 value, Span<char> output)
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
				return false;
			}
			*c = '0';
			*(c + 1) = 0;
			return true;
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
			return true;
		}
	}
	return false;
}


bool toCString(i32 value, Span<char> output)
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
		return toCString((unsigned int)value, Span(c, length));
	}
	return false;
}

bool toCString(i64 value, Span<char> output)
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
		return toCString((u64)value, Span(c, length));
	}
	return false;
}

bool toCString(u64 value, Span<char> output)
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
				return false;
			}
			*c = '0';
			*(c + 1) = 0;
			return true;
		}
		while (value > 0 && length > 1)
		{
			*c = value % 10 + '0';
			value = value / 10;
			--length;
			++c;
		}
		if (length > 0)
		{
			reverse(num_start, (int)(c - num_start));
			*c = 0;
			return true;
		}
	}
	return false;
}

bool toCStringHex(u8 value, Span<char> output)
{
	if (output.length() < 2)
	{
		return false;
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
	return true;
}

bool toCString(u32 value, Span<char> output)
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
				return false;
			}
			*c = '0';
			*(c + 1) = 0;
			return true;
		}
		while (value > 0 && length > 1)
		{
			*c = value % 10 + '0';
			value = value / 10;
			--length;
			++c;
		}
		if (length > 0)
		{
			reverse(num_start, (int)(c - num_start));
			*c = 0;
			return true;
		}
	}
	return false;
}

static bool increment(const char* output, char* end, bool is_space_after)
{
	char carry = 1;
	char* c = end;
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
	if (carry && is_space_after)
	{
		char* c = end + 1; // including '\0' at the end of the String
		while (c >= output)
		{
			*(c + 1) = *c;
			--c;
		}
		++c;
		*c = '1';
		return true;
	}
	return !carry;
}


bool toCString(float value, Span<char> out, int after_point)
{
	char* output = out.begin();
	u32 length = out.length();
	if (length < 2)
	{
		return false;
	}
	if (value < 0)
	{
		*output = '-';
		++output;
		value = -value;
		--length;
	}
	// int part
	int exponent = value == 0 ? 0 : (int)log10(value);
	double num = value;
	char* c = output;
	if (num  < 1 && num > -1 && length > 1)
	{
		*c = '0';
		++c;
		--length;
	}
	else
	{
		while ((num >= 1 || exponent >= 0) && length > 1)
		{
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
	if (length > 1 && after_point > 0)
	{
		*c = '.';
		++c;
		--length;
	}
	else if (length > 0 && after_point == 0)
	{
		*c = 0;
		return true;
	}
	else
	{
		return false;
	}
	while (length > 1 && after_point > 0)
	{
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
		increment(output, c - 1, length > 1);
	return true;
}

bool toCString(bool value, Span<char> output) { return copyString(output, value ? "true" : "false"); }


bool toCString(double value, Span<char> out, int after_point)
{
	char* output = out.begin();
	u32 length = out.length();
	if (length < 2)
	{
		return false;
	}
	if (value < 0)
	{
		*output = '-';
		++output;
		value = -value;
		--length;
	}
	// int part
	int exponent = value == 0 ? 0 : (int)log10(value);
	double num = value;
	char* c = output;
	if (num  < 1 && num > -1 && length > 1)
	{
		*c = '0';
		++c;
		--length;
	}
	else
	{
		while ((num >= 1 || exponent >= 0) && length > 1)
		{
			double power = (double)pow(10, exponent);
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
	if (length > 1 && after_point > 0)
	{
		*c = '.';
		++c;
		--length;
	}
	else if (length > 0 && after_point == 0)
	{
		*c = 0;
		return true;
	}
	else
	{
		return false;
	}
	while (length > 1 && after_point > 0)
	{
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
		increment(output, c - 1, length > 1);
	return true;
}


bool startsWith(const char* str, const char* prefix) {
	const char* lhs = str;
	const char* rhs = prefix;
	while (*rhs && *lhs && *lhs == *rhs) {
		++lhs;
		++rhs;
	}

	return *rhs == 0;
}

static char toLower(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
	return c;
}

bool startsWithInsensitive(const char* str, const char* prefix) {
	const char* lhs = str;
	const char* rhs = prefix;
	while (*rhs && *lhs && toLower(*lhs) == toLower(*rhs)) {
		++lhs;
		++rhs;
	}

	return *rhs == 0;
}


} // namespace Lumix

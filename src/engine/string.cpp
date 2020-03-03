#include "string.h"
#include "engine/allocator.h"
#include "engine/crt.h"


namespace Lumix
{


String::String(IAllocator& allocator)
	: m_allocator(allocator)
{
	m_cstr = nullptr;
	m_size = 0;
}


String::String(const String& rhs, int start, i32 length)
	: m_allocator(rhs.m_allocator)
{
	m_size = length - start <= rhs.m_size ? length : rhs.m_size - start;
	m_cstr = (char*)m_allocator.allocate((m_size + 1) * sizeof(char));
	memcpy(m_cstr, rhs.m_cstr + start, m_size * sizeof(char));
	m_cstr[m_size] = 0;
}


String::String(Span<const char> rhs, IAllocator& allocator)
	: m_allocator(allocator)
{
	m_size = rhs.length();
	m_cstr = (char*)m_allocator.allocate((m_size + 1) * sizeof(char));
	memcpy(m_cstr, rhs.begin(), m_size * sizeof(char));
	m_cstr[m_size] = 0;
}


String::String(const String& rhs)
	: m_allocator(rhs.m_allocator)
{
	m_cstr = (char*)m_allocator.allocate((rhs.m_size + 1) * sizeof(char));
	m_size = rhs.m_size;
	memcpy(m_cstr, rhs.m_cstr, m_size * sizeof(char));
	m_cstr[m_size] = 0;
}


String::String(String&& rhs)
	: m_allocator(rhs.m_allocator)
{
	m_cstr = rhs.m_cstr;
	m_size = rhs.m_size;
	rhs.m_cstr = nullptr;
	rhs.m_size = 0;
}


String::String(const char* rhs, IAllocator& allocator)
	: m_allocator(allocator)
{
	m_size = stringLength(rhs);
	m_cstr = (char*)m_allocator.allocate((m_size + 1) * sizeof(char));
	memcpy(m_cstr, rhs, sizeof(char) * (m_size + 1));
}


String::~String() { m_allocator.deallocate(m_cstr); }


char String::operator[](int index) const
{
	ASSERT(index >= 0 && index < m_size);
	return m_cstr[index];
}


void String::set(const char* rhs, int size)
{
	if (rhs < m_cstr || rhs >= m_cstr + m_size)
	{
		m_allocator.deallocate(m_cstr);
		m_size = size;
		m_cstr = (char*)m_allocator.allocate(m_size + 1);
		memcpy(m_cstr, rhs, size);
		m_cstr[size] = '\0';
	}
}


void String::operator=(const String& rhs)
{
	if (&rhs != this)
	{
		m_allocator.deallocate(m_cstr);
		m_cstr = (char*)m_allocator.allocate((rhs.m_size + 1) * sizeof(char));
		m_size = rhs.m_size;
		memcpy(m_cstr, rhs.m_cstr, sizeof(char) * (m_size + 1));
	}
}


void String::operator=(const char* rhs)
{
	if (rhs < m_cstr || rhs >= m_cstr + m_size)
	{
		m_allocator.deallocate(m_cstr);
		if (rhs)
		{
			m_size = stringLength(rhs);
			m_cstr = (char*)m_allocator.allocate((m_size + 1) * sizeof(char));
			memcpy(m_cstr, rhs, sizeof(char) * (m_size + 1));
		}
		else
		{
			m_size = 0;
			m_cstr = nullptr;
		}
	}
}


bool String::operator!=(const String& rhs) const
{
	return compareString(m_cstr, rhs.m_cstr) != 0;
}


bool String::operator!=(const char* rhs) const
{
	return compareString(m_cstr, rhs) != 0;
}


bool String::operator==(const String& rhs) const
{
	return compareString(m_cstr, rhs.m_cstr) == 0;
}


bool String::operator==(const char* rhs) const
{
	return compareString(m_cstr, rhs) == 0;
}


bool String::operator<(const String& rhs) const
{
	return compareString(m_cstr, rhs.m_cstr) < 0;
}


bool String::operator>(const String& rhs) const
{
	return compareString(m_cstr, rhs.m_cstr) > 0;
}


String String::substr(int start, int length) const
{
	return String(*this, start, length);
}


void String::resize(int size)
{
	ASSERT(size > 0);
	if (size <= 0) return;
	
	m_cstr = (char*)m_allocator.reallocate(m_cstr, size);
	m_size = size - 1;
	m_cstr[size - 1] = '\0';
}


String& String::cat(Span<const char> value)
{
	const u32 length = value.length();
	if (value.begin() < m_cstr || value.begin() >= m_cstr + m_size)
	{
		if (m_cstr)
		{
			i32 new_size = m_size + length;
			char* new_cstr = (char*)m_allocator.allocate(new_size + 1);
			memcpy(new_cstr, m_cstr, sizeof(char) * m_size + 1);
			m_allocator.deallocate(m_cstr);
			m_cstr = new_cstr;
			m_size = new_size;
			catNString(Span(m_cstr, m_size + 1), value.begin(), length);
		}
		else
		{
			m_size = length;
			m_cstr = (char*)m_allocator.allocate(m_size + 1);
			copyNString(Span(m_cstr, m_size + 1), value.begin(), length);
		}
	}
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


void String::eraseAt(int position)
{
	if (position < 0 || position >= m_size) return;
	memmove(m_cstr + position, m_cstr + position + 1, m_size - position - 1);
	--m_size;
	m_cstr[m_size] = '\0';
}


void String::insert(int position, const char* value)
{
	if (m_cstr)
	{
		int value_len = stringLength(value);
		i32 new_size = m_size + value_len;
		char* new_cstr = (char*)m_allocator.allocate(new_size + 1);
		if (position > 0) memcpy(new_cstr, m_cstr, sizeof(char) * position);
		if (value_len > 0) memcpy(new_cstr + position, value, sizeof(char) * value_len);
		memcpy(new_cstr + position + value_len, m_cstr + position, sizeof(char) * (m_size - position) + 1);
		
		m_allocator.deallocate(m_cstr);
		m_cstr = new_cstr;
		m_size = new_size;
	}
	else
	{
		m_size = stringLength(value);
		m_cstr = (char*)m_allocator.allocate(m_size + 1);
		copyString(Span(m_cstr, m_size + 1), value);
	}
}


String& String::cat(const char* rhs)
{
	if (rhs < m_cstr || rhs >= m_cstr + m_size)
	{
		if (m_cstr)
		{
			i32 new_size = m_size + stringLength(rhs);
			char* new_cstr = (char*)m_allocator.allocate(new_size + 1);
			memcpy(new_cstr, m_cstr, sizeof(char) * m_size + 1);
			m_allocator.deallocate(m_cstr);
			m_cstr = new_cstr;
			m_size = new_size;
			catString(Span(m_cstr, m_size + 1), rhs);
		}
		else
		{
			m_size = stringLength(rhs);
			m_cstr = (char*)m_allocator.allocate(m_size + 1);
			copyString(Span(m_cstr, m_size + 1), rhs);
		}
	}
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



bool endsWith(const char* str, const char* substr)
{
	int len = stringLength(str);
	int len2 = stringLength(substr);
	if (len2 > len) return false;
	return equalStrings(str + len - len2, substr);
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
		dst.m_begin = dst.m_end = nullptr;
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

const char* fromCString(Span<const char> input, Ref<i32> value)
{
	i64 val;
	const char* ret = fromCString(input, Ref(val));
	value = (i32)val;
	return ret;
}

const char* fromCString(Span<const char> input, Ref<i64> value)
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

const char* fromCString(Span<const char> input, Ref<u16> value)
{
	u32 tmp;
	const char* ret = fromCString(input, Ref(tmp));
	value = u16(tmp);
	return ret;
}

const char* fromCString(Span<const char> input, Ref<u32> value)
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

const char* fromCStringOctal(Span<const char> input, Ref<u32> value)
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

const char* fromCString(Span<const char> input, Ref<u64> value)
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

const char* fromCString(Span<const char> input, Ref<bool> value)
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
	float num = value;
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
			float power = powf(10, (float)exponent);
			char digit = (char)floor(num / power);
			num -= digit * power;
			*c = digit + '0';
			--exponent;
			--length;
			++c;
		}
	}
	// decimal part
	float dec_part = num;
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


bool startsWith(const char* str, const char* prefix)
{
	const char* lhs = str;
	const char* rhs = prefix;
	while (*rhs && *lhs && *lhs == *rhs)
	{
		++lhs;
		++rhs;
	}

	return *rhs == 0;
}


} // namespace Lumix

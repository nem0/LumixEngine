#include "string.h"
#include <cstring>
#include <cmath>


namespace Lumix
{


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
	return strnicmp(lhs, rhs, length);
#else
	return strncasecmp(lhs, rhs, length);
#endif
}


int compareString(const char* lhs, const char* rhs)
{
	return strcmp(lhs, rhs);
}


int stringLength(const char* str)
{
	return (int)strlen(str);
}


void moveMemory(void* dest, const void* src, size_t count)
{
	memmove(dest, src, count);
}


void setMemory(void* ptr, uint8 value, size_t num)
{
	memset(ptr, value, num);
}


void copyMemory(void* dest, const void* src, size_t count)
{
	memcpy(dest, src, count);
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


bool makeLowercase(char* destination, int length, const char* source)
{
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


bool copyNString(char* destination, int length, const char* source, int source_len)
{
	if (!source)
	{
		return false;
	}

	while (*source && length > 1 && source_len)
	{
		*destination = *source;
		--source_len;
		--length;
		++destination;
		++source;
	}
	if (length > 0)
	{
		*destination = 0;
		return *source == '\0' || source_len == 0;
	}
	return false;
}


bool copyString(char* destination, int length, const char* source)
{
	if (!source || length < 1)
	{
		return false;
	}

	while (*source && length > 1)
	{
		*destination = *source;
		--length;
		++destination;
		++source;
	}
	if (length > 0)
	{
		*destination = 0;
		return *source == '\0';
	}
	return false;
}


const char* reverseFind(const char* str, const char* from, char c)
{
	const char* tmp = from == nullptr ? nullptr : from - 1;
	if (tmp == nullptr)
	{
		tmp = str;
		while (*tmp)
		{
			++tmp;
		}
	}
	while (tmp >= str && *tmp != c)
	{
		--tmp;
	}
	if (tmp >= str)
	{
		return tmp;
	}
	return nullptr;
}


bool catNString(char* destination,
				int length,
				const char* source,
				int source_len)
{
	while (*destination && length)
	{
		--length;
		++destination;
	}
	return copyNString(destination, length, source, source_len);
}


bool catString(char* destination, int length, const char* source)
{
	while (*destination && length)
	{
		--length;
		++destination;
	}
	return copyString(destination, length, source);
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

const char* fromCString(const char* input, int length, int32* value)
{
	int64 val;
	const char* ret = fromCString(input, length, &val);
	*value = (int32)val;
	return ret;
}

const char* fromCString(const char* input, int length, int64* value)
{
	if (length > 0)
	{
		const char* c = input;
		*value = 0;
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
			*value *= 10;
			*value += *c - '0';
			++c;
			--length;
		}
		if (input[0] == '-')
		{
			*value = -*value;
		}
		return c;
	}
	return nullptr;
}

const char* fromCString(const char* input, int length, uint32* value)
{
	if (length > 0)
	{
		const char* c = input;
		*value = 0;
		if (*c == '-')
		{
			return nullptr;
		}
		while (length && *c >= '0' && *c <= '9')
		{
			*value *= 10;
			*value += *c - '0';
			++c;
			--length;
		}
		return c;
	}
	return nullptr;
}


bool toCStringPretty(int32 value, char* output, int length)
{
	char* c = output;
	if (length > 0)
	{
		if (value < 0)
		{
			value = -value;
			--length;
			*c = '-';
			++c;
		}
		return toCStringPretty((unsigned int)value, c, length);
	}
	return false;
}


bool toCStringPretty(uint32 value, char* output, int length)
{
	return toCStringPretty(uint64(value), output, length);
}


bool toCStringPretty(uint64 value, char* output, int length)
{
	char* c = output;
	char* num_start = output;
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


bool toCString(int32 value, char* output, int length)
{
	char* c = output;
	if (length > 0)
	{
		if (value < 0)
		{
			value = -value;
			--length;
			*c = '-';
			++c;
		}
		return toCString((unsigned int)value, c, length);
	}
	return false;
}

bool toCString(int64 value, char* output, int length)
{
	char* c = output;
	if (length > 0)
	{
		if (value < 0)
		{
			value = -value;
			--length;
			*c = '-';
			++c;
		}
		return toCString((uint64)value, c, length);
	}
	return false;
}

bool toCString(uint64 value, char* output, int length)
{
	char* c = output;
	char* num_start = output;
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

bool toCStringHex(uint8 value, char* output, int length)
{
	if (length < 2)
	{
		return false;
	}
	uint8 first = value / 16;
	if (first > 9)
	{
		output[0] = 'A' + first - 10;
	}
	else
	{
		output[0] = '0' + first;
	}
	uint8 second = value % 16;
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

bool toCString(uint32 value, char* output, int length)
{
	char* c = output;
	char* num_start = output;
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

static bool increment(char* output, char* end, bool is_space_after)
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
		char* c = end + 1; // including '\0' at the end of the string
		while (c >= output)
		{
			*(c + 1) = *c;
			++c;
		}
		*c = '1';
		return true;
	}
	return !carry;
}


bool toCString(float value, char* output, int length, int after_point)
{
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
			float power = (float)pow(10, exponent);
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
	if (length > 0)
	{
		*c = 0;
		if ((int)(dec_part + 0.5f))
			increment(output, c - 1, length > 1);
	}
	else
	{
		return false;
	}
	return true;
}


char* trimmed(char* str)
{
	while (*str && (*str == '\t' || *str == ' '))
	{
		++str;
	}
	return str;
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


} // ~namespace Lumix

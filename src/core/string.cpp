#include "string.h"
#include <cmath>

namespace Lumix
{

	bool copyString(char* destination, int length, const char* source)
	{
		while (*source && length)
		{
			*destination = *source;
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

	bool catCString(char* destination, int length, const char* source)
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

	bool fromCString(const char* input, int length, int32_t* value)
	{
		int64_t val;
		bool b = fromCString(input, length, &val);
		*value = (int32_t)val;
		return b;
	}

	bool fromCString(const char* input, int length, int64_t* value)
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
					return false;
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
			return true;
		}
		return false;
	}

	bool fromCString(const char* input, int length, uint32_t* value)
	{
		if (length > 0)
		{
			const char* c = input;
			*value = 0;
			if (*c == '-')
			{
				return false;
			}
			while (length && *c >= '0' && *c <= '9')
			{
				*value *= 10;
				*value += *c - '0';
				++c;
				--length;
			}
			return true;
		}
		return false;
	}


	bool toCString(int32_t value, char* output, int length)
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

	bool toCString(int64_t value, char* output, int length)
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
				reverse(num_start, c - num_start);
				*c = 0;
				return true;
			}
		}
		return false;
	}

	bool toCStringHex(uint8_t value, char* output, int length)
	{
		if (length < 2)
		{
			return false;
		}
		uint8_t first = value / 16;
		if (first > 9)
		{
			output[0] = 'A' + first - 10;
		}
		else
		{
			output[0] = '0' + first;
		}
		uint8_t second = value % 16;
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

	bool toCString(uint32_t value, char* output, int length)
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
				reverse(num_start, c - num_start);
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
		if (length < 1)
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
		int exponent = (int)log10(value);
		float num = value;
		char* c = output;
		while ((num >= 1 || exponent >= 0) && length > 0)
		{
			float power = (float)pow(10, exponent);
			char digit = (char)floor(num / power);
			num -= digit * power;
			*c = digit + '0';
			--exponent;
			--length;
			++c;
		}
		// decimal part
		float dec_part = num;
		if (length >= 1 && after_point > 0)
		{
			*c = '.';
			++c;
			--length;
		}
		else if (length >= 1 && after_point == 0)
		{
			*c = 0;
			return true;
		}
		else
		{
			return false;
		}
		while (length > 0 && after_point > 0)
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
			if ((int)(dec_part + 0.5f)) increment(output, c - 1, length > 1);
		}
		else
		{
			return false;
		}
		return true;
	}

	

} // ~namespace Lumix

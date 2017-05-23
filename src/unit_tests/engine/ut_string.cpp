#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/string.h"
#include <cstdio>


using namespace Lumix;


void UT_string(const char* params)
{
	char tmp[100];
	char tmp2[100];
	for (i32 i = -100; i < 100; ++i)
	{
		toCString(i, tmp, 100);
		sprintf(tmp2, "%d", i);
		LUMIX_EXPECT(equalStrings(tmp, tmp2));
	}

	for (u32 i = 0; i < 100; ++i)
	{
		toCString(i, tmp, 100);
		sprintf(tmp2, "%u", i);
		LUMIX_EXPECT(equalStrings(tmp, tmp2));
	}

	toCStringPretty(123456, tmp, sizeof(tmp));
	LUMIX_EXPECT(equalStrings(tmp, "123 456"));

	toCStringPretty(-123456, tmp, sizeof(tmp));
	LUMIX_EXPECT(equalStrings(tmp, "-123 456"));

	toCStringPretty(123456789, tmp, sizeof(tmp));
	LUMIX_EXPECT(equalStrings(tmp, "123 456 789"));

	toCStringPretty(3456789, tmp, sizeof(tmp));
	LUMIX_EXPECT(equalStrings(tmp, "3 456 789"));

	toCString((unsigned int)0xffffFFFF, tmp, 1000);
	sprintf(tmp2, "%u", (unsigned int)0xffffFFFF);
	LUMIX_EXPECT(equalStrings(tmp, tmp2));

	for (float i = 100; i > -100; i -= 0.27f)
	{
		toCString(i, tmp, 100, 6);
		sprintf(tmp2, "%f", i);
		LUMIX_EXPECT(equalStrings(tmp, tmp2));
	}

	float f = (float)0xffffFFFF;
	f += 1000;
	toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT(equalStrings(tmp, tmp2));

	f = -f;
	toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT(equalStrings(tmp, tmp2));

	LUMIX_EXPECT(stristr("abc", "def") == nullptr);
	LUMIX_EXPECT(stristr("abc", "abcdef") == nullptr);
	LUMIX_EXPECT(stristr("abcdef", "abd") == nullptr);
	LUMIX_EXPECT(stristr("abcdef", "bcdf") == nullptr);
	LUMIX_EXPECT(stristr("ABC", "def") == nullptr);
	LUMIX_EXPECT(stristr("abc", "abc") != nullptr);
	LUMIX_EXPECT(stristr("abc", "ABC") != nullptr);
	LUMIX_EXPECT(stristr("ABC", "abc") != nullptr);
	LUMIX_EXPECT(stristr("aBc", "AbC") != nullptr);
	LUMIX_EXPECT(stristr("ABc", "aBC") != nullptr);
	LUMIX_EXPECT(stristr("XYABcmn", "aBc") != nullptr);
	LUMIX_EXPECT(stristr("XYABcmn", "cMn") != nullptr);
}

REGISTER_TEST("unit_tests/engine/string", UT_string, "")

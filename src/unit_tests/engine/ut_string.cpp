#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/string.h"
#include <cstdio>


void UT_string(const char* params)
{
	char tmp[100];
	char tmp2[100];
	for (int32 i = -100; i < 100; ++i)
	{
		Lumix::toCString(i, tmp, 100);
		sprintf(tmp2, "%d", i);
		LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));
	}

	for (uint32 i = 0; i < 100; ++i)
	{
		Lumix::toCString(i, tmp, 100);
		sprintf(tmp2, "%u", i);
		LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));
	}

	Lumix::toCStringPretty(123456, tmp, sizeof(tmp));
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "123 456"));

	Lumix::toCStringPretty(-123456, tmp, sizeof(tmp));
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "-123 456"));

	Lumix::toCStringPretty(123456789, tmp, sizeof(tmp));
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "123 456 789"));

	Lumix::toCStringPretty(3456789, tmp, sizeof(tmp));
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "3 456 789"));

	Lumix::toCString((unsigned int)0xffffFFFF, tmp, 1000);
	sprintf(tmp2, "%u", (unsigned int)0xffffFFFF);
	LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));

	for (float i = 100; i > -100; i -= 0.27f)
	{
		Lumix::toCString(i, tmp, 100, 6);
		sprintf(tmp2, "%f", i);
		LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));
	}

	float f = (float)0xffffFFFF;
	f += 1000;
	Lumix::toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));

	f = -f;
	Lumix::toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT(Lumix::equalStrings(tmp, tmp2));

	LUMIX_EXPECT(Lumix::stristr("abc", "def") == nullptr);
	LUMIX_EXPECT(Lumix::stristr("abc", "abcdef") == nullptr);
	LUMIX_EXPECT(Lumix::stristr("abcdef", "abd") == nullptr);
	LUMIX_EXPECT(Lumix::stristr("abcdef", "bcdf") == nullptr);
	LUMIX_EXPECT(Lumix::stristr("ABC", "def") == nullptr);
	LUMIX_EXPECT(Lumix::stristr("abc", "abc") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("abc", "ABC") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("ABC", "abc") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("aBc", "AbC") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("ABc", "aBC") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("XYABcmn", "aBc") != nullptr);
	LUMIX_EXPECT(Lumix::stristr("XYABcmn", "cMn") != nullptr);
}

REGISTER_TEST("unit_tests/engine/string", UT_string, "")

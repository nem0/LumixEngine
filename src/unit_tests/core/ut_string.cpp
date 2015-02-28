#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/string.h"
#include <cstdio>


void UT_string(const char* params)
{
	char tmp[100];
	char tmp2[100];
	for (int32_t i = -100; i < 100; ++i)
	{
		Lumix::toCString(i, tmp, 100);
		sprintf(tmp2, "%d", i);
		LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);
	}
	
	for (uint32_t i = 0; i < 100; ++i)
	{
		Lumix::toCString(i, tmp, 100);
		sprintf(tmp2, "%u", i);
		LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);
	}
	
	Lumix::toCStringPretty(123456, tmp, sizeof(tmp));
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)"123 456");

	Lumix::toCStringPretty(-123456, tmp, sizeof(tmp));
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)"-123 456");

	Lumix::toCStringPretty(123456789, tmp, sizeof(tmp));
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)"123 456 789");

	Lumix::toCStringPretty(3456789, tmp, sizeof(tmp));
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)"3 456 789");

	Lumix::toCString((unsigned int)0xffffFFFF, tmp, 1000);
	sprintf(tmp2, "%u", (unsigned int)0xffffFFFF);
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);

	for (float i = 100; i > -100; i -= 0.27f)
	{
		Lumix::toCString(i, tmp, 100, 6);
		sprintf(tmp2, "%f", i);
		LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);
	}

	float f = (float)0xffffFFFF;
	f += 1000;
	Lumix::toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);

	f = -f;
	Lumix::toCString(f, tmp, 100, 6);
	sprintf(tmp2, "%f", f);
	LUMIX_EXPECT_EQ((const char*)tmp, (const char*)tmp2);
}

REGISTER_TEST("unit_tests/core/string", UT_string, "")
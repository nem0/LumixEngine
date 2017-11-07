#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/math_utils.h"
#include <cfloat>


using namespace Lumix;


void UT_math_utils_abs_signum(const char* params)
{
	DefaultAllocator allocator;

	LUMIX_EXPECT(Math::abs(-1) == 1);
	LUMIX_EXPECT(Math::abs(1) == 1);
	LUMIX_EXPECT(Math::abs(0) == 0);
	LUMIX_EXPECT(Math::abs(-100) == 100);
	LUMIX_EXPECT(Math::abs(100) == 100);

	LUMIX_EXPECT(Math::abs(-1.0f) == 1.0f);
	LUMIX_EXPECT(Math::abs(1.0f) == 1.0f);
	LUMIX_EXPECT(Math::abs(0.0f) == 0.0f);
	LUMIX_EXPECT(Math::abs(-100.0f) == 100.0f);
	LUMIX_EXPECT(Math::abs(100.0f) == 100.0f);
	LUMIX_EXPECT(Math::abs(-1.2f) == 1.2f);
	LUMIX_EXPECT(Math::abs(3.7f) == 3.7f);

	LUMIX_EXPECT(Math::signum(-1) == -1);
	LUMIX_EXPECT(Math::signum(1) == 1);
	LUMIX_EXPECT(Math::signum(0) == 0);
	LUMIX_EXPECT(Math::signum(-1.0f) == -1.0f);
	LUMIX_EXPECT(Math::signum(1.0f) == 1.0f);
	LUMIX_EXPECT(Math::signum(0.0f) == 0.0f);

	for(int i = 1; i < 50; ++i)
	{
		LUMIX_EXPECT(Math::signum(i) == 1);
		LUMIX_EXPECT(Math::signum(-i) == -1);
	}

	for(float f = 1; f < 50; f += 0.3f)
	{
		LUMIX_EXPECT(Math::signum(f) == 1.0f);
		LUMIX_EXPECT(Math::signum(-f) == -1.0f);
	}
}


void UT_math_utils_clamp(const char* params)
{
	LUMIX_EXPECT(Math::clamp(1, 1, 1) == 1);
	LUMIX_EXPECT(Math::clamp(1, 0, 1) == 1);
	LUMIX_EXPECT(Math::clamp(1, 0, 2) == 1);
	LUMIX_EXPECT(Math::clamp(1, 1, 2) == 1);
	LUMIX_EXPECT(Math::clamp(0, 1, 2) == 1);
	LUMIX_EXPECT(Math::clamp(3, 1, 2) == 2);

	LUMIX_EXPECT(Math::clamp(1.0f, 1.0f, 1.0f) == 1.0f);
	LUMIX_EXPECT(Math::clamp(1.0f, 0.0f, 1.0f) == 1.0f);
	LUMIX_EXPECT(Math::clamp(1.0f, 0.0f, 2.0f) == 1.0f);
	LUMIX_EXPECT(Math::clamp(1.0f, 1.0f, 2.0f) == 1.0f);
	LUMIX_EXPECT(Math::clamp(0.0f, 1.0f, 2.0f) == 1.0f);
	LUMIX_EXPECT(Math::clamp(3.0f, 1.0f, 2.0f) == 2.0f);
}


void UT_math_utils_degrees_to_radians(const char* params)
{
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(0), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(22.5f), Math::PI * 0.125f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-22.5f), -Math::PI * 0.125f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(45), Math::PI * 0.25f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-45), -Math::PI * 0.25f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(90), Math::PI * 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-90), -Math::PI * 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(180), Math::PI, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-180), -Math::PI, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(360), Math::PI * 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-360), -Math::PI * 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(720), Math::PI * 4, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::degreesToRadians(-720), -Math::PI * 4, 0.001f);
}


void UT_math_utils_ease_in_out(const char* params)
{
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(0), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(0.1f), 0.02f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(0.25f), 0.125f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(0.5f), 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(0.75f), 0.875f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(1), 1, 0.001f);

	for(float f = 0; f <= 0.5f; f += 0.01f)
	{
		LUMIX_EXPECT_CLOSE_EQ(Math::easeInOut(f), 1 - Math::easeInOut(1 - f), 0.001f);
	}

	for(float f = 0; f <= 0.42f; f += 0.01f)
	{
		LUMIX_EXPECT(Math::easeInOut(f) < Math::easeInOut(f + 0.01f));
		LUMIX_EXPECT(Math::easeInOut(f + 0.01f) - Math::easeInOut(f) < Math::easeInOut(f + 0.02f) - Math::easeInOut(f + 0.01f));
	}
}


void UT_math_utils_is_pow_of_two(const char* params)
{
	unsigned int x = 1;
	for(int i = 0; i < 31; ++i)
	{
		LUMIX_EXPECT(Math::isPowOfTwo(x));
		LUMIX_EXPECT(!(Math::isPowOfTwo(x + 1) && x != 1));
		x <<= 1;
	}
	LUMIX_EXPECT(!Math::isPowOfTwo(0));
	LUMIX_EXPECT(!Math::isPowOfTwo(-1));
	LUMIX_EXPECT(!Math::isPowOfTwo(-2));
	LUMIX_EXPECT(!Math::isPowOfTwo(-3));
	LUMIX_EXPECT(!Math::isPowOfTwo(-4));

	for(int i = 1025; i < 2048; ++i)
	{
		LUMIX_EXPECT(!Math::isPowOfTwo(i));
	}
}



void UT_math_utils_float_flip(const char* params)
{
	for (float f = 0; f < 10000; f += 0.1f)
	{
		u32 i = Math::floatFlip(*(u32*)&f);
		float f2 = f + 0.1f;
		u32 i2 = Math::floatFlip(*(u32*)&f2);
		LUMIX_EXPECT((f < f2) == (i < i2));
		LUMIX_EXPECT((f > f2) == (i > i2));
	}
	for (float f = 0; f > -10000; f -= 0.1f)
	{
		u32 i = Math::floatFlip(*(u32*)&f);
		float f2 = f + 0.1f;
		u32 i2 = Math::floatFlip(*(u32*)&f2);
		LUMIX_EXPECT((f < f2) == (i < i2));
		LUMIX_EXPECT((f > f2) == (i > i2));
	}
	for (float f = FLT_MIN; f < FLT_MAX; f *= 2)
	{
		u32 i = Math::floatFlip(*(u32*)&f);
		float f2 = f + 0.1f;
		u32 i2 = Math::floatFlip(*(u32*)&f2);
		LUMIX_EXPECT((f < f2) == (i < i2));
		LUMIX_EXPECT((f > f2) == (i > i2));
	}
}


void UT_math_utils_min_max(const char* params)
{
	LUMIX_EXPECT(Math::minimum(0, 1) == 0);
	LUMIX_EXPECT(Math::minimum(-1, 1) == -1);
	LUMIX_EXPECT(Math::minimum(-1, 0) == -1);
	LUMIX_EXPECT(Math::minimum(-1, -2) == -2);
	LUMIX_EXPECT(Math::minimum(0, -2) == -2);
	LUMIX_EXPECT(Math::minimum(3, -2) == -2);
	LUMIX_EXPECT(Math::minimum(0xffffFFFE, 0xffffFFFF) == 0xFFFFfffe);

	LUMIX_EXPECT(Math::maximum(0, 1) == 1);
	LUMIX_EXPECT(Math::maximum(-1, 1) == 1);
	LUMIX_EXPECT(Math::maximum(-1, 0) == 0);
	LUMIX_EXPECT(Math::maximum(-1, -2) == -1);
	LUMIX_EXPECT(Math::maximum(0, -2) == 0);
	LUMIX_EXPECT(Math::maximum(3, -2) == 3);
	LUMIX_EXPECT(Math::maximum(0xffffFFFE, 0xffffFFFF) == 0xFFFFffff);

	for(int i = -100; i < 100; ++i)
	{
		LUMIX_EXPECT(Math::minimum(i, i + 1) == i);
	}
	for(float f = -100; f < 100; f += 0.3f)
	{
		LUMIX_EXPECT(Math::minimum(f, f + 0.2f) == f);
		LUMIX_EXPECT(Math::minimum(f, f + 0.3f) == f);
	}
}


REGISTER_TEST("unit_tests/engine/math_utils/abs_signum", UT_math_utils_abs_signum, "")
REGISTER_TEST("unit_tests/engine/math_utils/clamp", UT_math_utils_clamp, "")
REGISTER_TEST("unit_tests/engine/math_utils/math_utils_degrees_to_radians", UT_math_utils_degrees_to_radians, "")
REGISTER_TEST("unit_tests/engine/math_utils/math_utils_ease_in_out", UT_math_utils_ease_in_out, "")
REGISTER_TEST("unit_tests/engine/math_utils/is_pow_of_two", UT_math_utils_is_pow_of_two, "")
REGISTER_TEST("unit_tests/engine/math_utils/min_max", UT_math_utils_min_max, "")
REGISTER_TEST("unit_tests/engine/math_utils/float_flip", UT_math_utils_float_flip, "")

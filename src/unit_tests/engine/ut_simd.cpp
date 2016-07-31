#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/simd.h"


using namespace Lumix;


static const float LUMIX_ALIGN_BEGIN(16) c0[4] LUMIX_ALIGN_END(16) = { 0, 1, 2, 3 };
static const float LUMIX_ALIGN_BEGIN(16) c1[4] LUMIX_ALIGN_END(16) = { 5, 9, -15, 0 };
static const float LUMIX_ALIGN_BEGIN(16) c2[4] LUMIX_ALIGN_END(16) = { 5, 10, -13, 3 };
static const float LUMIX_ALIGN_BEGIN(16) c3[4] LUMIX_ALIGN_END(16) = { -5, -8, 17, 3 };
static const float LUMIX_ALIGN_BEGIN(16) c4[4] LUMIX_ALIGN_END(16) = { 0, 9, -30, 0 };

static const float LUMIX_ALIGN_BEGIN(16) c5[4] LUMIX_ALIGN_END(16) = { 3, 9, 0.25f, 1 };
static const float LUMIX_ALIGN_BEGIN(16) c6[4] LUMIX_ALIGN_END(16) = { 0, 1/9.0f, 2/0.25f, 3 };
static const float LUMIX_ALIGN_BEGIN(16) c7[4] LUMIX_ALIGN_END(16) = { 1/5.0f, 1/10.0f, 1/-13.0f, 1/3.0f };

static const float LUMIX_ALIGN_BEGIN(16) c8[4] LUMIX_ALIGN_END(16) = { 4, 9, 1, 0 };
static const float LUMIX_ALIGN_BEGIN(16) c9[4] LUMIX_ALIGN_END(16) = { 2, 3, 1, 0 };

static const float LUMIX_ALIGN_BEGIN(16) c10[4] LUMIX_ALIGN_END(16) = { 4, 9, 1, 100 };
static const float LUMIX_ALIGN_BEGIN(16) c11[4] LUMIX_ALIGN_END(16) = { 1/2.0f, 1/3.0f, 1, 1/10.0f };

static const float LUMIX_ALIGN_BEGIN(16) c12[4] LUMIX_ALIGN_END(16) = { 0, 1, -15, 0 };
static const float LUMIX_ALIGN_BEGIN(16) c13[4] LUMIX_ALIGN_END(16) = { 5, 9, 2, 3 };

#define LUMIX_EXPECT_FLOAT4_EQUAL(a, b) \
	do { \
	LUMIX_EXPECT_CLOSE_EQ((a)[0], (b)[0], 0.001f); \
	LUMIX_EXPECT_CLOSE_EQ((a)[1], (b)[1], 0.001f); \
	LUMIX_EXPECT_CLOSE_EQ((a)[2], (b)[2], 0.001f); \
	LUMIX_EXPECT_CLOSE_EQ((a)[3], (b)[3], 0.001f); \
	} while(false) \


void UT_simd_load_store(const char* params)
{
	float4 a = f4Load(c0);
	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, a);
	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c0);
}


void UT_simd_add(const char* params)
{
	float4 a = f4Load(c0);
	float4 b = f4Load(c1);
	float4 res = f4Add(a, b);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c2);
}


void UT_simd_sub(const char* params)
{
	float4 a = f4Load(c0);
	float4 b = f4Load(c1);
	float4 res = f4Sub(a, b);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c3);
}


void UT_simd_mul(const char* params)
{
	float4 a = f4Load(c0);
	float4 b = f4Load(c1);
	float4 res = f4Mul(a, b);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c4);
}


void UT_simd_div(const char* params)
{
	float4 a = f4Load(c0);
	float4 b = f4Load(c5);
	float4 res = f4Div(a, b);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c6);
}


void UT_simd_rcp(const char* params)
{
	float4 a = f4Load(c2);
	float4 res = f4Rcp(a);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c7);
}


void UT_simd_sqrt(const char* params)
{
	float4 a = f4Load(c8);
	float4 res = f4Sqrt(a);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c9);

	float4 b = f4Mul(res, res);
	f4Store(tmp, b);
	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c8);
}


void UT_simd_rsqrt(const char* params)
{
	float4 a = f4Load(c10);
	float4 res = f4Rsqrt(a);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c11);
}


void UT_simd_min_max(const char* params)
{
	float4 a = f4Load(c0);
	float4 b = f4Load(c1);
	float4 res = f4Min(a, b);

	float LUMIX_ALIGN_BEGIN(16) tmp[4] LUMIX_ALIGN_END(16);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c12);

	res = f4Max(a, b);
	f4Store(tmp, res);

	LUMIX_EXPECT_FLOAT4_EQUAL(tmp, c13);
}


REGISTER_TEST("unit_tests/engine/simd/load_store", UT_simd_load_store, "")
REGISTER_TEST("unit_tests/engine/simd/add", UT_simd_add, "")
REGISTER_TEST("unit_tests/engine/simd/sub", UT_simd_sub, "")
REGISTER_TEST("unit_tests/engine/simd/mul", UT_simd_mul, "")
REGISTER_TEST("unit_tests/engine/simd/div", UT_simd_div, "")
REGISTER_TEST("unit_tests/engine/simd/rcp", UT_simd_rcp, "")
REGISTER_TEST("unit_tests/engine/simd/sqrt", UT_simd_sqrt, "")
REGISTER_TEST("unit_tests/engine/simd/rsqrt", UT_simd_rsqrt, "")
REGISTER_TEST("unit_tests/engine/simd/min_max", UT_simd_min_max, "")

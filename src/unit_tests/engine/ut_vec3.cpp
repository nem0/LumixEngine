#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/vec.h"
#include <cmath>


using namespace Lumix;


void UT_vec3(const char* params)
{
	DefaultAllocator allocator;

	Vec3 v(0, 0, 0);
	LUMIX_EXPECT(v.x == 0);
	LUMIX_EXPECT(v.y == 0);
	LUMIX_EXPECT(v.z == 0);

	LUMIX_EXPECT_CLOSE_EQ(v.length(), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.squaredLength(), 0, 0.001f);

	Vec3 v2(1, 0, 0);
	LUMIX_EXPECT(v2.x == 1);
	LUMIX_EXPECT(v2.y == 0);
	LUMIX_EXPECT(v2.z == 0);

	LUMIX_EXPECT_CLOSE_EQ(v2.length(), 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.squaredLength(), 1, 0.001f);
	v2.normalize();
	LUMIX_EXPECT_CLOSE_EQ(v2.length(), 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.squaredLength(), 1, 0.001f);

	v2.set(1, 2, 3);
	LUMIX_EXPECT(v2.x == 1);
	LUMIX_EXPECT(v2.y == 2);
	LUMIX_EXPECT(v2.z == 3);

	LUMIX_EXPECT_CLOSE_EQ(v2.length(), 3.74165750f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.squaredLength(), 3.74165750f * 3.74165750f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.normalized().length(), 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.normalized().y, 2 * v2.normalized().x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.normalized().z, 3 * v2.normalized().x, 0.001f);
	v2.normalize();
	LUMIX_EXPECT_CLOSE_EQ(v2.length(), 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.y, 2 * v2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v2.z, 3 * v2.x, 0.001f);

	v = -Vec3(1, 2, 3);
	LUMIX_EXPECT_CLOSE_EQ(v.x, -1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, -2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, -3, 0.001f);

	v = v + Vec3(4, 5, 6);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 3, 0.001f);

	v = v * 2;
	LUMIX_EXPECT_CLOSE_EQ(v.x, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 6, 0.001f);

	LUMIX_EXPECT_CLOSE_EQ(dotProduct(Vec3(1, 0, 0), Vec3(0, 1, 0)), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(dotProduct(Vec3(1, 0, 0), Vec3(1, 0, 0)), 1, 0.001f);

	v = crossProduct(Vec3(1, 0, 0), Vec3(0, 1, 0));
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 1, 0.001f);

	lerp(Vec3(1, 0, 0), Vec3(0, 1, 0), &v, 0);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

	lerp(Vec3(1, 0, 0), Vec3(0, 1, 0), &v, 1);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

	lerp(Vec3(1, 0, 0), Vec3(0, 1, 0), &v, 0.5f);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

}

REGISTER_TEST("unit_tests/engine/vec3", UT_vec3, "")

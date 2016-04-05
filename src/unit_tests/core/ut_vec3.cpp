#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/vec.h"
#include <cmath>


void UT_vec3(const char* params)
{
	Lumix::DefaultAllocator allocator;

	Lumix::Vec3 v(0, 0, 0);
	LUMIX_EXPECT(v.x == 0);
	LUMIX_EXPECT(v.y == 0);
	LUMIX_EXPECT(v.z == 0);

	LUMIX_EXPECT_CLOSE_EQ(v.length(), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.squaredLength(), 0, 0.001f);

	Lumix::Vec3 v2(1, 0, 0);
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

	Lumix::Vec3 v3(1, 0, 0);
	v3.rotateX(0);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);
	v3.rotateX(10);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);
	v3.rotateX(-5);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);

	v3.rotateY(0);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);
	v3.rotateY(Lumix::Math::PI * 2);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);
	v3.rotateY(Lumix::Math::PI);
	LUMIX_EXPECT_CLOSE_EQ(v3.x, -1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v3.z, 0, 0.001f);

	for (float f = 0; f < Lumix::Math::PI * 2; f += 0.01f)
	{
		float x = cos(f);
		float z = -sin(f);
		v3.set(1, 0, 0);
		v3.rotateY(f);
		LUMIX_EXPECT_CLOSE_EQ(v3.x, x, 0.001f);
		LUMIX_EXPECT_CLOSE_EQ(v3.y, 0, 0.001f);
		LUMIX_EXPECT_CLOSE_EQ(v3.z, z, 0.001f);
	}

	for (float f = 0; f < Lumix::Math::PI * 2; f += 0.01f)
	{
		float y = cos(f);
		float z = sin(f);
		v3.set(0, 1, 0);
		v3.rotateX(f);
		LUMIX_EXPECT_CLOSE_EQ(v3.x, 0, 0.001f);
		LUMIX_EXPECT_CLOSE_EQ(v3.y, y, 0.001f);
		LUMIX_EXPECT_CLOSE_EQ(v3.z, z, 0.001f);
	}

	v = -Lumix::Vec3(1, 2, 3);
	LUMIX_EXPECT_CLOSE_EQ(v.x, -1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, -2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, -3, 0.001f);

	v = v + Lumix::Vec3(4, 5, 6);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 3, 0.001f);

	v = v * 2;
	LUMIX_EXPECT_CLOSE_EQ(v.x, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 6, 0.001f);

	LUMIX_EXPECT_CLOSE_EQ(Lumix::dotProduct(Lumix::Vec3(1, 0, 0), Lumix::Vec3(0, 1, 0)), 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(Lumix::dotProduct(Lumix::Vec3(1, 0, 0), Lumix::Vec3(1, 0, 0)), 1, 0.001f);

	v = Lumix::crossProduct(Lumix::Vec3(1, 0, 0), Lumix::Vec3(0, 1, 0));
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 1, 0.001f);

	Lumix::lerp(Lumix::Vec3(1, 0, 0), Lumix::Vec3(0, 1, 0), &v, 0);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

	Lumix::lerp(Lumix::Vec3(1, 0, 0), Lumix::Vec3(0, 1, 0), &v, 1);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

	Lumix::lerp(Lumix::Vec3(1, 0, 0), Lumix::Vec3(0, 1, 0), &v, 0.5f);
	LUMIX_EXPECT_CLOSE_EQ(v.x, 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 0.5f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 0, 0.001f);

}

REGISTER_TEST("unit_tests/core/vec3", UT_vec3, "")
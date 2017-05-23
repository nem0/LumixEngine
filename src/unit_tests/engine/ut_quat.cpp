#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/quat.h"
#include "engine/matrix.h"
#include "engine/math_utils.h"


#include <cmath>


using namespace Lumix;


void UT_quat(const char* params)
{
	Quat q1, q2;
	Matrix mtx;

	q1.set(1.0f, 0.0f, 0.0f, 0.0f);
	LUMIX_EXPECT_CLOSE_EQ(q1.x, 1.0f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, 0.0f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, 0.0f, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, 0.0f, 0.001f);

	mtx = q1.toMatrix();
	q2 = mtx.getRotation();
	LUMIX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1.set(0, 0, 1, 0);
	mtx = q1.toMatrix();
	q2 = mtx.getRotation();
	LUMIX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1 = Quat(Vec3(1, 0, 0), Math::PI / 4);
	mtx = q1.toMatrix();
	q2 = mtx.getRotation();
	LUMIX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1 = Quat(Vec3(1, 1, 0), Math::PI / 6);
	q1.normalize();
	mtx = q1.toMatrix();
	q2 = mtx.getRotation();
	LUMIX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1.set(0.923879862f, 0, 0, -0.382683545f);
	mtx = q1.toMatrix();
	q2 = mtx.getRotation();
	LUMIX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	Quat qeuler;
	for (float x = -Math::PI + 0.01f; x < Math::PI; x += 0.1f)
	{
		for (float y = -Math::PI + 0.01f; y < Math::PI; y += 0.1f)
		{
			float x = 0;
			for (float z = -Math::PI + 0.01f; z < Math::PI; z += 0.1f)
			{
				qeuler.fromEuler(Vec3(x, y, z));
				Vec3 u(0, 1, 0);
				u = qeuler.rotate(u);
				Vec3 euler = qeuler.toEuler();

				LUMIX_EXPECT_CLOSE_EQ(euler.x, x, 0.001f);
				LUMIX_EXPECT_CLOSE_EQ(euler.y, y, 0.001f);
				LUMIX_EXPECT_CLOSE_EQ(euler.z, z, 0.001f);
			}
		}
	}
}

REGISTER_TEST("unit_tests/engine/quat", UT_quat, "")

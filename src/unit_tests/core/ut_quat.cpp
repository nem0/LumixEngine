#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/quat.h"
#include "core/matrix.h"
#include "core/math_utils.h"

#include <cmath>


void UT_quat(const char* params)
{
	Lumix::Quat q1, q2;
	Lumix::Matrix mtx;

	q1.set(1.0f, 0.0f, 0.0f, 0.0f);
	LUX_EXPECT_CLOSE_EQ(q1.x, 1.0f, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, 0.0f, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, 0.0f, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, 0.0f, 0.001f);

	q1.toMatrix(mtx);
	mtx.getRotation(q2);
	LUX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1.set(0, 0, 1, 0);
	q1.toMatrix(mtx);
	mtx.getRotation(q2);
	LUX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1 = Lumix::Quat(Lumix::Vec3(1, 0, 0), Lumix::Math::PI / 4);
	q1.toMatrix(mtx);
	mtx.getRotation(q2);
	LUX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1 = Lumix::Quat(Lumix::Vec3(1, 1, 0), Lumix::Math::PI / 6);
	q1.normalize();
	q1.toMatrix(mtx);
	mtx.getRotation(q2);
	LUX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);

	q1.set(0.923879862f, 0, 0, -0.382683545f);
	q1.toMatrix(mtx);
	mtx.getRotation(q2);
	LUX_EXPECT_CLOSE_EQ(q1.x, q2.x, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.y, q2.y, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.z, q2.z, 0.001f);
	LUX_EXPECT_CLOSE_EQ(q1.w, q2.w, 0.001f);
}

REGISTER_TEST("unit_tests/core/quat", UT_quat, "")
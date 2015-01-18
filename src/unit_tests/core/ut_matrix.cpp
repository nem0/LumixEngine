#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/matrix.h"
#include "core/quat.h"


void expectSameMatrices(const Lumix::Matrix& mtx1, const Lumix::Matrix& mtx2)
{
	for (int i = 0; i < 16; ++i)
	{
		LUMIX_EXPECT_CLOSE_EQ(*((&mtx1.m11) + i), *((&mtx2.m11) + i), 0.001f);
	}
}

void UT_matrix(const char* params)
{
	Lumix::DefaultAllocator allocator;

	for (int i = 0; i < 16; ++i)
	{
		LUMIX_EXPECT_CLOSE_EQ(*((&Lumix::Matrix::IDENTITY.m11) + i), i % 5 == 0 ? 1.0f : 0.0f, 0.001f);
	}
	Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	LUMIX_EXPECT_CLOSE_EQ(mtx.determinant(), 1.0f, 0.001f);
	
	mtx.inverse();
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	mtx.fastInverse();
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	mtx.transpose();
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().z, 0, 0.001f);

	Lumix::Quat rot;
	mtx.getRotation(rot);
	LUMIX_EXPECT_CLOSE_EQ(rot.x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(rot.y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(rot.z, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(rot.w, 1, 0.001f);

	LUMIX_EXPECT_CLOSE_EQ(mtx.getXVector().x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getXVector().y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getXVector().z, 0, 0.001f);

	LUMIX_EXPECT_CLOSE_EQ(mtx.getYVector().x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getYVector().y, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getYVector().z, 0, 0.001f);

	LUMIX_EXPECT_CLOSE_EQ(mtx.getZVector().x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getZVector().y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getZVector().z, 1, 0.001f);

	mtx.multiply3x3(1.0f);
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	Lumix::Vec3 v = mtx.multiplyPosition(Lumix::Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(v.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 3, 0.001f);

	mtx.setTranslation(Lumix::Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m41, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m42, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m43, 3, 0.001f);

	mtx.setIdentity();
	expectSameMatrices(mtx, Lumix::Matrix::IDENTITY);

	mtx.translate(Lumix::Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m41, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m42, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m43, 3, 0.001f);
	mtx.setIdentity();

	mtx.setXVector(Lumix::Vec3(3, 2, 1));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m11, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m12, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m13, 1, 0.001f);

	mtx.setYVector(Lumix::Vec3(6, 5, 4));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m21, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m22, 5, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m23, 4, 0.001f);

	mtx.setZVector(Lumix::Vec3(9, 8, 7));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m31, 9, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m32, 8, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m33, 7, 0.001f);

	Lumix::Matrix old = mtx;
	expectSameMatrices(mtx, old);
	mtx.transpose();
	mtx.transpose();
	expectSameMatrices(mtx, old);

	mtx.inverse();
	mtx.inverse();
	expectSameMatrices(mtx, old);

	mtx.setIdentity();
	mtx.multiply3x3(2.0f);
	old = mtx;
	mtx.inverse();
	mtx.inverse();
	expectSameMatrices(mtx, old);

	for (int i = 0; i < 16; ++i)
	{
		*((&mtx.m11) + i) = i;
	}

	old = mtx;
	mtx = mtx * Lumix::Matrix::IDENTITY;
	expectSameMatrices(mtx, old);
	mtx = Lumix::Matrix::IDENTITY * mtx;
	expectSameMatrices(mtx, old);
}

REGISTER_TEST("unit_tests/core/matrix", UT_matrix, "")
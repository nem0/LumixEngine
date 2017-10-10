#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/matrix.h"
#include "engine/quat.h"


using namespace Lumix;


void expectSameMatrices(const Matrix& mtx1, const Matrix& mtx2)
{
	for (int i = 0; i < 16; ++i)
	{
		LUMIX_EXPECT_CLOSE_EQ(*((&mtx1.m11) + i), *((&mtx2.m11) + i), 0.001f);
	}
}

void UT_matrix(const char* params)
{
	DefaultAllocator allocator;

	for (int i = 0; i < 16; ++i)
	{
		LUMIX_EXPECT_CLOSE_EQ(*((&Matrix::IDENTITY.m11) + i), i % 5 == 0 ? 1.0f : 0.0f, 0.001f);
	}
	Matrix mtx = Matrix::IDENTITY;
	expectSameMatrices(mtx, Matrix::IDENTITY);

	LUMIX_EXPECT_CLOSE_EQ(mtx.determinant(), 1.0f, 0.001f);
	
	mtx.inverse();
	expectSameMatrices(mtx, Matrix::IDENTITY);

	mtx.fastInverse();
	expectSameMatrices(mtx, Matrix::IDENTITY);

	mtx.transpose();
	expectSameMatrices(mtx, Matrix::IDENTITY);

	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().x, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().y, 0, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.getTranslation().z, 0, 0.001f);

	Quat rot = mtx.getRotation();
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
	expectSameMatrices(mtx, Matrix::IDENTITY);

	Vec3 v = mtx.transformPoint(Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(v.x, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.y, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(v.z, 3, 0.001f);

	mtx.setTranslation(Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m41, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m42, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m43, 3, 0.001f);

	mtx.setIdentity();
	expectSameMatrices(mtx, Matrix::IDENTITY);

	mtx.translate(Vec3(1, 2, 3));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m41, 1, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m42, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m43, 3, 0.001f);
	mtx.setIdentity();

	mtx.setXVector(Vec3(3, 2, 1));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m11, 3, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m12, 2, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m13, 1, 0.001f);

	mtx.setYVector(Vec3(6, 5, 4));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m21, 6, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m22, 5, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m23, 4, 0.001f);

	mtx.setZVector(Vec3(9, 8, 7));
	LUMIX_EXPECT_CLOSE_EQ(mtx.m31, 9, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m32, 8, 0.001f);
	LUMIX_EXPECT_CLOSE_EQ(mtx.m33, 7, 0.001f);

	Matrix old = mtx;
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
		*((&mtx.m11) + i) = (float)i;
	}

	old = mtx;
	mtx = mtx * Matrix::IDENTITY;
	expectSameMatrices(mtx, old);
	mtx = Matrix::IDENTITY * mtx;
	expectSameMatrices(mtx, old);

	Matrix euler;
	euler.fromEuler(0, 0, 0);
	expectSameMatrices(euler, Matrix::IDENTITY);

	Matrix check_matrix;
	check_matrix = Matrix::IDENTITY;
	check_matrix.m11 = -1;
	check_matrix.m33 = -1;
	euler.fromEuler(3.14159265f, 0, 0);
	expectSameMatrices(euler, check_matrix);

	check_matrix = Matrix::IDENTITY;
	check_matrix.m11 = -1;
	check_matrix.m22 = -1;
	euler.fromEuler(0, 0, 3.14159265f);
	expectSameMatrices(euler, check_matrix);

	check_matrix = Matrix::IDENTITY;
	check_matrix.m22 = -1;
	check_matrix.m33 = -1;
	euler.fromEuler(0, 3.14159265f, 0);
	expectSameMatrices(euler, check_matrix);
}


REGISTER_TEST("unit_tests/engine/matrix", UT_matrix, "")

#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/geometry.h"


void UT_aabb(const char* params)
{
	Lumix::AABB aabb1;
	Lumix::AABB aabb2(Lumix::Vec3(0, 0, 0), Lumix::Vec3(1, 1, 1));

	LUMIX_EXPECT(aabb2.getMin().x == 0);
	LUMIX_EXPECT(aabb2.getMin().y == 0);
	LUMIX_EXPECT(aabb2.getMin().z == 0);

	LUMIX_EXPECT(aabb2.getMax().x == 1);
	LUMIX_EXPECT(aabb2.getMax().y == 1);
	LUMIX_EXPECT(aabb2.getMax().z == 1);

	aabb1 = aabb2;
	LUMIX_EXPECT(aabb1.getMin().x == aabb2.getMin().x);
	LUMIX_EXPECT(aabb1.getMin().y == aabb2.getMin().y);
	LUMIX_EXPECT(aabb1.getMin().z == aabb2.getMin().z);

	LUMIX_EXPECT(aabb1.getMax().x == aabb2.getMax().x);
	LUMIX_EXPECT(aabb1.getMax().y == aabb2.getMax().y);
	LUMIX_EXPECT(aabb1.getMax().z == aabb2.getMax().z);

	Lumix::Vec3 points[8];
	aabb2.getCorners(Lumix::Matrix::IDENTITY, points);
	LUMIX_EXPECT(points[0].x == 0);
	LUMIX_EXPECT(points[0].y == 0);
	LUMIX_EXPECT(points[0].z == 0);

	LUMIX_EXPECT(points[1].x == 0);
	LUMIX_EXPECT(points[1].y == 0);
	LUMIX_EXPECT(points[1].z == 1);

	LUMIX_EXPECT(points[2].x == 0);
	LUMIX_EXPECT(points[2].y == 1);
	LUMIX_EXPECT(points[2].z == 0);

	LUMIX_EXPECT(points[3].x == 0);
	LUMIX_EXPECT(points[3].y == 1);
	LUMIX_EXPECT(points[3].z == 1);

	LUMIX_EXPECT(points[4].x == 1);
	LUMIX_EXPECT(points[4].y == 0);
	LUMIX_EXPECT(points[4].z == 0);

	LUMIX_EXPECT(points[5].x == 1);
	LUMIX_EXPECT(points[5].y == 0);
	LUMIX_EXPECT(points[5].z == 1);

	LUMIX_EXPECT(points[6].x == 1);
	LUMIX_EXPECT(points[6].y == 1);
	LUMIX_EXPECT(points[6].z == 0);

	LUMIX_EXPECT(points[7].x == 1);
	LUMIX_EXPECT(points[7].y == 1);
	LUMIX_EXPECT(points[7].z == 1);

	Lumix::AABB aabb3(Lumix::Vec3(0, 0, 0), Lumix::Vec3(1, 1, 1));
	Lumix::AABB aabb4(Lumix::Vec3(1, 2, 3), Lumix::Vec3(2, 3, 4));
	Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
	mtx.setTranslation(Lumix::Vec3(1, 2, 3));
	aabb3.transform(mtx);

	LUMIX_EXPECT(aabb3.getMin().x == aabb4.getMin().x);
	LUMIX_EXPECT(aabb3.getMin().y == aabb4.getMin().y);
	LUMIX_EXPECT(aabb3.getMin().z == aabb4.getMin().z);

	LUMIX_EXPECT(aabb3.getMax().x == aabb4.getMax().x);
	LUMIX_EXPECT(aabb3.getMax().y == aabb4.getMax().y);
	LUMIX_EXPECT(aabb3.getMax().z == aabb4.getMax().z);
}

REGISTER_TEST("unit_tests/core/aabb", UT_aabb, "")
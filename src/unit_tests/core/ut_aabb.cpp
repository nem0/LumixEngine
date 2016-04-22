#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/core/geometry.h"


void UT_aabb(const char* params)
{
	Lumix::AABB aabb1;
	Lumix::AABB aabb2(Lumix::Vec3(0, 0, 0), Lumix::Vec3(1, 1, 1));

	LUMIX_EXPECT(aabb2.min.x == 0);
	LUMIX_EXPECT(aabb2.min.y == 0);
	LUMIX_EXPECT(aabb2.min.z == 0);

	LUMIX_EXPECT(aabb2.max.x == 1);
	LUMIX_EXPECT(aabb2.max.y == 1);
	LUMIX_EXPECT(aabb2.max.z == 1);

	aabb1 = aabb2;
	LUMIX_EXPECT(aabb1.min.x == aabb2.min.x);
	LUMIX_EXPECT(aabb1.min.y == aabb2.min.y);
	LUMIX_EXPECT(aabb1.min.z == aabb2.min.z);

	LUMIX_EXPECT(aabb1.max.x == aabb2.max.x);
	LUMIX_EXPECT(aabb1.max.y == aabb2.max.y);
	LUMIX_EXPECT(aabb1.max.z == aabb2.max.z);

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

	LUMIX_EXPECT(aabb3.min.x == aabb4.min.x);
	LUMIX_EXPECT(aabb3.min.y == aabb4.min.y);
	LUMIX_EXPECT(aabb3.min.z == aabb4.min.z);

	LUMIX_EXPECT(aabb3.max.x == aabb4.max.x);
	LUMIX_EXPECT(aabb3.max.y == aabb4.max.y);
	LUMIX_EXPECT(aabb3.max.z == aabb4.max.z);
}

REGISTER_TEST("unit_tests/core/aabb", UT_aabb, "")
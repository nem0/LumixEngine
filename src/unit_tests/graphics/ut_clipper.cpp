#include "unit_tests/suite/lux_unit_tests.h"

#include "core/sphere.h"
#include "core/vec3.h"
#include "core/frustum.h"
#include "graphics/culling_system.h"

namespace
{

	struct TestFrustum
	{
		Lux::Vec3 pos;
		Lux::Vec3 dir;
		Lux::Vec3 up;
		float fov;
		float ratio;
		float near;
		float far;
	};

	TestFrustum test_frustum = {
		{ 0.f, 0.f, -5.f },
		{ 0.f, 0.f, -1.f },
		{ 0.f, 1.f, 0.f },
		60.f,
		2.32378864f,
		10.f,
		100.f
	};

	void UT_culling_system(const char* params)
	{
		Lux::Array<Lux::Sphere> spheres;
		for (float i = 0.f; i < 1500.f; i += 15.f)
		{
			spheres.push(Lux::Sphere(i, 0.f, 50.f, 5.f));
		}

		Lux::Frustum clipping_frustum;
		clipping_frustum.compute(
			test_frustum.pos, 
			test_frustum.dir, 
			test_frustum.up, 
			test_frustum.fov, 
			test_frustum.ratio, 
			test_frustum.near, 
			test_frustum.far);

		Lux::CullingSystem culling_system;

		culling_system.create();
		culling_system.insert(spheres);
		culling_system.cullToFrustum(clipping_frustum);

		const Lux::Array<bool>& result = culling_system.getResult();
		for (int i = 0; i < result.size(); i++)
		{
			LUX_EXPECT_EQ(result[i], i < 6);
		}

		culling_system.destroy();
	}
}

REGISTER_TEST("unit_tests/graphics/culling_system", UT_culling_system, "");

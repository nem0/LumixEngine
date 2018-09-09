#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/timer.h"
#include "engine/log.h"

#include "renderer/culling_system.h"


using namespace Lumix;


namespace
{

	struct TestFrustum
	{
		Vec3 pos;
		Vec3 dir;
		Vec3 up;
		float fov;
		float ratio;
		float near;
		float far;
	};

	TestFrustum test_frustum =
	{
		{ 0.f, 0.f, -5.f },
		{ 0.f, 0.f, 1.f },
		{ 0.f, 1.f, 0.f },
		60.f,
		2.32378864f,
		10.f,
		100.f
	};


	void UT_culling_system_async(const char* params)
	{
		DefaultAllocator allocator;
		JobSystem::init(allocator);
		Array<Sphere> spheres(allocator);
		Array<EntityRef> model_instances(allocator);
		int model_instance = 0;
		for(float i = 0.f; i < 30000000.0f; i += 15.f)
		{
			spheres.push(Sphere(i, 0.f, 50.f, 5.f));
			model_instances.push({model_instance});
			++model_instance;
		}

		Frustum clipping_frustum;
		clipping_frustum.computePerspective(
			test_frustum.pos,
			test_frustum.dir,
			test_frustum.up,
			Math::degreesToRadians(test_frustum.fov),
			test_frustum.ratio,
			test_frustum.near,
			test_frustum.far);

		CullingSystem* culling_system;
		{
			culling_system = CullingSystem::create(allocator);
			culling_system->insert(spheres, model_instances);

			ScopedTimer timer("Culling System Async", allocator);
			
			CullingSystem::Results result(allocator);
			culling_system->cull(clipping_frustum, 1, result);

			for (int i = 0; i < result.size(); i++)
			{
				const CullingSystem::Subresults& subresult = result[i];
				for (int j = 0; j < subresult.size(); ++j)
				{
					LUMIX_EXPECT(subresult[i].index < 6);
				}
			}
		}

		CullingSystem::destroy(*culling_system);
		JobSystem::shutdown();
	}
}

REGISTER_TEST("unit_tests/graphics/culling_system_async", UT_culling_system_async, "");

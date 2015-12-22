#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/sphere.h"
#include "core/vec.h"
#include "core/frustum.h"
#include "core/timer.h"
#include "core/log.h"

#include "core/MTJD/manager.h"

#include "renderer/culling_system.h"

namespace
{

	struct TestFrustum
	{
		Lumix::Vec3 pos;
		Lumix::Vec3 dir;
		Lumix::Vec3 up;
		float fov;
		float ratio;
		float near;
		float far;
	};

	TestFrustum test_frustum =
	{
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
		Lumix::DefaultAllocator allocator;
		Lumix::Array<Lumix::Sphere> spheres(allocator);
		Lumix::Array<Lumix::ComponentIndex> renderables(allocator);
		int renderable = 0;
		for (float i = 0.f; i < 30000000.0f; i += 15.f)
		{
			spheres.push(Lumix::Sphere(i, 0.f, 50.f, 5.f));
			renderables.push(renderable);
			++renderable;
		}

		Lumix::Frustum clipping_frustum;
		clipping_frustum.computePerspective(
			test_frustum.pos,
			test_frustum.dir,
			test_frustum.up,
			Lumix::Math::degreesToRadians(test_frustum.fov),
			test_frustum.ratio,
			test_frustum.near,
			test_frustum.far);

		Lumix::CullingSystem* culling_system;
		{
			Lumix::MTJD::Manager* mtjd_manager = Lumix::MTJD::Manager::create(allocator);

			culling_system = Lumix::CullingSystem::create(*mtjd_manager, allocator);
			culling_system->insert(spheres, renderables);

			Lumix::ScopedTimer timer("Culling System", allocator);

			culling_system->cullToFrustum(clipping_frustum, 1);
			const Lumix::CullingSystem::Results& result = culling_system->getResult();

			for (int i = 0; i < result.size(); i++)
			{
				const Lumix::CullingSystem::Subresults& subresult = result[i];
				for (int j = 0; j < subresult.size(); ++j)
				{
					LUMIX_EXPECT(subresult[i] < 6);
				}
			}

			Lumix::MTJD::Manager::destroy(*mtjd_manager);
		}

		Lumix::CullingSystem::destroy(*culling_system);
	}

	void UT_culling_system_async(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::Array<Lumix::Sphere> spheres(allocator);
		Lumix::Array<Lumix::ComponentIndex> renderables(allocator);
		int renderable = 0;
		for(float i = 0.f; i < 30000000.0f; i += 15.f)
		{
			spheres.push(Lumix::Sphere(i, 0.f, 50.f, 5.f));
			renderables.push(renderable);
			++renderable;
		}

		Lumix::Frustum clipping_frustum;
		clipping_frustum.computePerspective(
			test_frustum.pos,
			test_frustum.dir,
			test_frustum.up,
			Lumix::Math::degreesToRadians(test_frustum.fov),
			test_frustum.ratio,
			test_frustum.near,
			test_frustum.far);

		Lumix::CullingSystem* culling_system;
		{
			Lumix::MTJD::Manager* mtjd_manager = Lumix::MTJD::Manager::create(allocator);

			culling_system = Lumix::CullingSystem::create(*mtjd_manager, allocator);
			culling_system->insert(spheres, renderables);

			Lumix::ScopedTimer timer("Culling System Async", allocator);

			culling_system->cullToFrustumAsync(clipping_frustum, 1);

			const Lumix::CullingSystem::Results& result = culling_system->getResult();

			for (int i = 0; i < result.size(); i++)
			{
				const Lumix::CullingSystem::Subresults& subresult = result[i];
				for (int j = 0; j < subresult.size(); ++j)
				{
					LUMIX_EXPECT(subresult[i] < 6);
				}
			}

			Lumix::MTJD::Manager::destroy(*mtjd_manager);
		}

		Lumix::CullingSystem::destroy(*culling_system);
	}
}

REGISTER_TEST("unit_tests/graphics/culling_system", UT_culling_system, "");
REGISTER_TEST("unit_tests/graphics/culling_system_async", UT_culling_system_async, "");

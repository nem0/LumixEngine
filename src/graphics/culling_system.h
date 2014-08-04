#pragma once

//// remove
#include "core/array.h"
#include "core/sphere.h"
//// remove

namespace Lumix
{
	namespace MTJD
	{
		class Manager;
	}
	struct Frustum;

	class LUMIX_ENGINE_API CullingSystem
	{
	public:
		typedef Array<Sphere> InputSpheres;
		typedef Array<int> Indexes;
		typedef Array<int> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(MTJD::Manager& mtjd_manager);
		static void destroy(CullingSystem& culling_system);

		virtual const Results& getResult() = 0;
		virtual const Results& getResultAsync() = 0;

		virtual void cullToFrustum(const Frustum& frustum) = 0;
		virtual void cullToFrustumAsync(const Frustum& frustum) = 0;

		virtual void addStatic(const Sphere& sphere, int index) = 0;

		//// test
		virtual void insert(const InputSpheres& spheres) = 0;
		//// test
	};
} // ~namespace Lux
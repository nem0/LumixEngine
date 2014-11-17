#pragma once

#include "core/array.h"
#include "core/sphere.h"

namespace Lumix
{
	namespace MTJD
	{
		class Manager;
	}
	class Frustum;

	class LUMIX_ENGINE_API CullingSystem
	{
	public:
		typedef Array<Sphere> InputSpheres;
		typedef Array<int> Subresults;
		typedef Array<Subresults> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(MTJD::Manager& mtjd_manager, IAllocator& allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;
		virtual const Results& getResult() = 0;
		virtual const Results& getResultAsync() = 0;

		virtual void cullToFrustum(const Frustum& frustum) = 0;
		virtual void cullToFrustumAsync(const Frustum& frustum) = 0;

		virtual void addStatic(const Sphere& sphere) = 0;
		virtual void removeStatic(int index) = 0;

		virtual void enableStatic(int index) = 0;
		virtual void disableStatic(int index) = 0;

		virtual void updateBoundingRadius(float radius, int index) = 0;
		virtual void updateBoundingPosition(const Vec3& position, int index) = 0;

		virtual void insert(const InputSpheres& spheres) = 0;
		virtual const InputSpheres& getSpheres() = 0;
	};
} // ~namespace Lux
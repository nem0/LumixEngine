#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	template <typename T> class Array;
	class IAllocator;
	struct Sphere;
	struct Vec3;


	namespace MTJD
	{
		class Manager;
	}
	struct Frustum;

	class LUMIX_RENDERER_API CullingSystem
	{
	public:
		typedef Array<Sphere> InputSpheres;
		typedef Array<ComponentHandle> Subresults;
		typedef Array<Subresults> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(MTJD::Manager& mtjd_manager, IAllocator& allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;
		virtual const Results& getResult() = 0;

		virtual void cullToFrustum(const Frustum& frustum, int64 layer_mask) = 0;
		virtual void cullToFrustumAsync(const Frustum& frustum, int64 layer_mask) = 0;

		virtual bool isAdded(ComponentHandle renderable) = 0;
		virtual void addStatic(ComponentHandle renderable, const Sphere& sphere) = 0;
		virtual void removeStatic(ComponentHandle renderable) = 0;

		virtual void setLayerMask(ComponentHandle renderable, int64 layer) = 0;
		virtual int64 getLayerMask(ComponentHandle renderable) = 0;

		virtual void updateBoundingRadius(float radius, ComponentHandle renderable) = 0;
		virtual void updateBoundingPosition(const Vec3& position, ComponentHandle renderable) = 0;

		virtual void insert(const InputSpheres& spheres, const Array<ComponentHandle>& renderables) = 0;
		virtual const Sphere& getSphere(ComponentHandle renderable) = 0;
	};
} // ~namespace Lux
#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	template <typename T> class Array;
	struct IAllocator;
	struct Sphere;
	struct Vec3;


	struct Frustum;

	class LUMIX_RENDERER_API CullingSystem
	{
	public:
		typedef Array<Sphere> InputSpheres;
		typedef Array<ComponentHandle> Subresults;
		typedef Array<Subresults> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(IAllocator& allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;
		virtual const Results& getResult() = 0;

		virtual Results& cull(const Frustum& frustum, u64 layer_mask) = 0;

		virtual bool isAdded(ComponentHandle model_instance) = 0;
		virtual void addStatic(ComponentHandle model_instance, const Sphere& sphere, u64 layer_mask) = 0;
		virtual void removeStatic(ComponentHandle model_instance) = 0;

		virtual void setLayerMask(ComponentHandle model_instance, u64 layer) = 0;
		virtual u64 getLayerMask(ComponentHandle model_instance) = 0;

		virtual void updateBoundingSphere(const Sphere& sphere, ComponentHandle model_instance) = 0;

		virtual void insert(const InputSpheres& spheres, const Array<ComponentHandle>& model_instances) = 0;
		virtual const Sphere& getSphere(ComponentHandle model_instance) = 0;
	};
} // ~namespace Lux
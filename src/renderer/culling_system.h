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
		typedef Array<Entity> Subresults;
		typedef Array<Subresults> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(IAllocator& allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;
		virtual const Results& getResult() = 0;

		virtual Results& cull(const Frustum& frustum, u64 layer_mask) = 0;

		virtual bool isAdded(Entity model_instance) = 0;
		virtual void addStatic(Entity model_instance, const Sphere& sphere, u64 layer_mask) = 0;
		virtual void removeStatic(Entity model_instance) = 0;

		virtual void setLayerMask(Entity model_instance, u64 layer) = 0;
		virtual u64 getLayerMask(Entity model_instance) = 0;

		virtual void updateBoundingSphere(const Sphere& sphere, Entity model_instance) = 0;

		virtual void insert(const InputSpheres& spheres, const Array<Entity>& model_instances) = 0;
		virtual const Sphere& getSphere(Entity model_instance) = 0;
	};
} // namespace Lux

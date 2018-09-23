#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	template <typename T> class Array;
	struct DVec3;
	struct Frustum;
	struct IAllocator;
	struct ShiftedFrustum;
	struct Sphere;
	struct Vec3;


	class LUMIX_RENDERER_API CullingSystem
	{
	public:
		typedef Array<Sphere> InputSpheres;
		typedef Array<u32> Subresults; // {type:8, entity:24}
		typedef Array<Subresults> Results;

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(IAllocator& allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;

		virtual void cull(const ShiftedFrustum& frustum, Results& result) = 0;

		virtual bool isAdded(EntityRef entity) = 0;
		virtual void add(EntityRef entity, u8 type, const DVec3& pos, float radius) = 0;
		virtual void remove(EntityRef entity) = 0;

		virtual void setPosition(EntityRef entity, const DVec3& pos) = 0;
		virtual void setRadius(EntityRef entity, float radius) = 0;

		virtual float getRadius(EntityRef entity) = 0;
	};
} // namespace Lux

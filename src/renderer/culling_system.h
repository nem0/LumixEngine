#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	template <typename T> class Array;
	struct DVec3;
	struct Frustum;
	struct IAllocator;
	class PageAllocator;
	struct ShiftedFrustum;
	struct Sphere;
	struct Vec3;

	struct CullResult {
		void free(PageAllocator& allocator);
		template <typename F>
		void forEach(F&& f) const {
			const CullResult* j = this;
			while (j) {
				j = j->header.next;
				for (u32 i = 0, c = j->header.count; i < c; ++i) {
					f(j->entities[i]);
				}
			}
		}
		
		struct {
			CullResult* next = nullptr;
			u32 count = 0;
		} header;
		EntityRef entities[(16384 - sizeof(header)) / sizeof(EntityRef)];
	};

	class LUMIX_RENDERER_API CullingSystem
	{
	public:

		CullingSystem() { }
		virtual ~CullingSystem() { }

		static CullingSystem* create(IAllocator& allocator, PageAllocator& page_allocator);
		static void destroy(CullingSystem& culling_system);

		virtual void clear() = 0;

		virtual CullResult* cull(const ShiftedFrustum& frustum, u8 type) = 0;

		virtual bool isAdded(EntityRef entity) = 0;
		virtual void add(EntityRef entity, u8 type, const DVec3& pos, float radius) = 0;
		virtual void remove(EntityRef entity) = 0;

		virtual void setPosition(EntityRef entity, const DVec3& pos) = 0;
		virtual void setRadius(EntityRef entity, float radius) = 0;

		virtual float getRadius(EntityRef entity) = 0;
	};
} // namespace Lux

#pragma once


#include "engine/lumix.h"


namespace Lumix
{

template <typename T> struct Array;
template <typename T> struct UniquePtr;
struct DVec3;
struct IAllocator;
struct PageAllocator;
struct ShiftedFrustum;
struct Sphere;
struct Vec3;

struct CullResult {
	void merge(CullResult* other) {
		CullResult** last = &header.next;
		while(*last) {
			last = &(*last)->header.next;
		}
		*last = other;
	}
	
	u32 count() const {
		u32 res = 0;
		const CullResult* j = this;
		while (j) {
			res += j->header.count;
			j = j->header.next;
		}
		return res;
	}

	void free(PageAllocator& allocator);

	template <typename F>
	void forEach(F&& f) const {
		const CullResult* j = this;
		while (j) {
			for (u32 i = 0, c = j->header.count; i < c; ++i) {
				f(j->entities[i]);
			}
			j = j->header.next;
		}
	}
	
	struct {
		CullResult* next = nullptr;
		u32 count = 0;
		u8 type;
	} header;

	EntityRef entities[(4096 - sizeof(header)) / sizeof(EntityRef)];
};

struct LUMIX_RENDERER_API CullingSystem
{
	CullingSystem() { }
	virtual ~CullingSystem() { }

	static UniquePtr<CullingSystem> create(IAllocator& allocator, PageAllocator& page_allocator);

	virtual void clear() = 0;

	virtual CullResult* cull(const ShiftedFrustum& frustum, u8 type) = 0;
	virtual CullResult* cull(const ShiftedFrustum& frustum) = 0;

	virtual bool isAdded(EntityRef entity) = 0;
	virtual void add(EntityRef entity, u8 type, const DVec3& pos, float radius) = 0;
	virtual void remove(EntityRef entity) = 0;

	virtual void setPosition(EntityRef entity, const DVec3& pos) = 0;
	virtual void setRadius(EntityRef entity, float radius) = 0;
	virtual void set(EntityRef entity, const DVec3& pos, float radius) = 0;

	virtual float getRadius(EntityRef entity) = 0;
};

} // namespace Lumix

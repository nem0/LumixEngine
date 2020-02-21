#pragma once


#include "engine/array.h"
#include "engine/lumix.h"
#include "engine/math.h"


namespace Lumix
{


struct Transform;
struct Vec3;
struct Viewport;
struct WorldEditor;
template <typename T> struct Array;


struct LUMIX_EDITOR_API Gizmo
{
	struct RenderData
	{
		RenderData(IAllocator& allocator) 
			: indices(allocator) 
			, vertices(allocator) 
			, cmds(allocator) 
		{}

		struct Vertex { Vec3 position; u32 color; };
		struct Cmd 
		{ 
			Matrix mtx;
			bool lines;
			u32 indices_offset;
			u32 indices_count;
			u32 vertices_offset;
			u32 vertices_count;
		};
		Array<u16> indices;
		Array<Vertex> vertices;
		Array<Cmd> cmds;
	};

	static Gizmo* create(WorldEditor& editor);
	static void destroy(Gizmo& gizmo);

	virtual ~Gizmo() {}

	virtual Vec3 getOffset() const = 0;
	virtual void setOffset(const Vec3& offset) = 0;
	virtual bool isActive() const = 0;
	virtual void add(EntityRef entity) = 0;
	virtual void getRenderData(RenderData* data, const Viewport& vp) = 0;
	virtual void setTranslateMode() = 0;
	virtual void setRotateMode() = 0;
	virtual void setScaleMode() = 0;
	virtual void setGlobalCoordSystem() = 0;
	virtual void setLocalCoordSystem() = 0;
	virtual int getStep() const = 0;
	virtual void setStep(int step) = 0;
	virtual void enableStep(bool enable) = 0;
	virtual bool isAutosnapDown() const = 0;
	virtual void setAutosnapDown(bool snap) = 0;
	virtual bool isTranslateMode() const = 0;
	virtual bool isRotateMode() const = 0;
	virtual bool isScaleMode() const = 0;
	virtual bool isLocalCoordSystem() const = 0;
	virtual bool isGlobalCoordSystem() const = 0;
	virtual void setPivotCenter() = 0;
	virtual void setPivotOrigin() = 0;
	virtual bool isPivotCenter() const = 0;
	virtual bool isPivotOrigin() const = 0;
	virtual void clearEntities() = 0;
	virtual void update(const Viewport& vp) = 0;
};


} // namespace Lumix

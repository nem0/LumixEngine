#pragma once


#include "engine/delegate.h"
#include "engine/hash.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "renderer/gpu/gpu.h"


struct lua_State;


namespace Lumix {

struct Path;
struct Renderer;
struct RenderModule;
struct Viewport;

struct PassState {
	Matrix projection;
	Matrix inv_projection;
	Matrix view;
	Matrix inv_view;
	Matrix view_projection;
	Matrix inv_view_projection;
	Vec4 view_dir;
	Vec4 camera_up;
	Vec4 camera_planes[6];
	Vec4 shadow_to_camera;
};

namespace UniformBuffer {
	enum {
		GLOBAL,
		PASS,
		MATERIAL,
		SHADOW,
		DRAWCALL,
		DRAWCALL2,

		COUNT
	};
}

struct LUMIX_RENDERER_API PipelineResource : Resource {
	static ResourceType TYPE;

	PipelineResource(const Path& path, ResourceManager& owner, Renderer& renderer, IAllocator& allocator);

	void unload() override;
	bool load(u64 size, const u8* mem) override;
	ResourceType getType() const override { return TYPE; }

	String content;
};


struct LUMIX_RENDERER_API Pipeline {
	struct CustomCommandHandler {
		Delegate<void ()> callback;
		char name[30];
		RuntimeHash hash;
	};

	static UniquePtr<Pipeline> create(Renderer& renderer, PipelineResource* resource, const char* define);
	
	virtual ~Pipeline() {}

	virtual bool render(bool only_2d) = 0;
	virtual void render3DUI(EntityRef e, const struct Draw2D& drawdata, Vec2 canvas_size, bool orient_to_cam) = 0;
	virtual void setWorld(struct World* world) = 0;
	virtual RenderModule* getModule() const = 0;
	virtual Renderer& getRenderer() const = 0;
	virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
	virtual bool isReady() const = 0;
	virtual const Path& getPath() = 0;
	virtual void callLuaFunction(const char* func) = 0;
	virtual void setViewport(const Viewport& viewport) = 0;
	virtual Viewport getViewport() = 0;
	virtual void define(const char* define, bool enable) = 0;
	virtual void setIndirectLightMultiplier(float value) = 0;

	virtual Draw2D& getDraw2D() = 0;
	virtual void clearDraw2D() = 0;
	virtual gpu::TextureHandle getOutput() = 0;
};

} // namespace Lumix

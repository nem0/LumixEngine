#pragma once


#include "engine/delegate.h"
#include "engine/math.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "renderer/gpu/gpu.h"


struct lua_State;


namespace Lumix
{

struct Draw2D;
struct IAllocator;
struct Mesh;
struct Path;
struct Renderer;
struct RenderScene;
struct Viewport;
template <typename T> struct Delegate;

struct PassState
{
	Matrix projection;
	Matrix inv_projection;
	Matrix view;
	Matrix inv_view;
	Matrix view_projection;
	Matrix inv_view_projection;
	Vec4 view_dir;
	Vec4 camera_planes[6];
};

struct LUMIX_RENDERER_API PipelineResource : Resource
{
	static ResourceType TYPE;

	PipelineResource(const Path& path, ResourceManager& owner, Renderer& renderer, IAllocator& allocator);

	void unload() override;
	bool load(u64 size, const u8* mem) override;
	ResourceType getType() const override { return TYPE; }

	Array<char> content;
};


struct LUMIX_RENDERER_API Pipeline
{
	struct Stats
	{
		u32 draw_call_count;
		u32 instance_count;
		u32 triangle_count;
	};

	struct CustomCommandHandler
	{
		Delegate<void ()> callback;
		char name[30];
		u32 hash;
	};

	static Pipeline* create(Renderer& renderer, PipelineResource* resource, const char* define, IAllocator& allocator);
	static void destroy(Pipeline* pipeline);

	virtual ~Pipeline() {}

	virtual bool render(bool only_2d) = 0;
	virtual void setUniverse(struct Universe* universe) = 0;
	virtual RenderScene* getScene() const = 0;
	virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
	virtual bool isReady() const = 0;
	virtual const Stats& getStats() const = 0;
	virtual const Path& getPath() = 0;
	virtual void callLuaFunction(const char* func) = 0;
	virtual void setViewport(const Viewport& viewport) = 0;
	virtual Viewport getViewport() = 0;
	virtual gpu::BufferHandle getDrawcallUniformBuffer() = 0;
	virtual void define(const char* define, bool enable) = 0;

	virtual Draw2D& getDraw2D() = 0;
	virtual void clearDraw2D() = 0;
	virtual gpu::TextureHandle getOutput() = 0;
};

} // namespace Lumix

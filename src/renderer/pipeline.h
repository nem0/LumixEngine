#pragma once

#include "core/delegate.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "engine/resource.h"
#include "core/string.h"
#include "renderer/gpu/gpu.h"


namespace Lumix {

struct Path;
struct Renderer;
struct RenderModule;
struct Shader;
struct Viewport;

struct CameraParams {
	ShiftedFrustum frustum;
	DVec3 pos;
	float lod_multiplier;
	bool is_shadow;
	Matrix view;
	Matrix projection;
};

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
	enum Enum {
		GLOBAL,
		PASS,
		MATERIAL,
		SHADOW,
		DRAWCALL,
		DRAWCALL2,

		COUNT
	};
}

struct BucketDesc {
	enum Sort {
		DEFAULT,
		DEPTH
	};

	const char* layer;
	Sort sort = Sort::DEFAULT;
	const char* define = nullptr;
	gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FUNCTION;
};

enum class PipelineType {
	SCENE_VIEW,
	GAME_VIEW,
	PROBE,
	PREVIEW,
	GUI_EDITOR
};

struct RenderBufferHandle {
	constexpr RenderBufferHandle() : value(0xffFFffFF) {}
	explicit constexpr RenderBufferHandle(u32 value) : value(value) {}
	operator u32() const { return value; }
	bool operator ==(const RenderBufferHandle& rhs) const { return value == rhs.value; }
	u32 value;
};

static constexpr RenderBufferHandle INVALID_RENDERBUFFER = RenderBufferHandle();

struct GBuffer {
	RenderBufferHandle A;
	RenderBufferHandle B;
	RenderBufferHandle C;
	RenderBufferHandle D;
	RenderBufferHandle DS;
};

struct RenderbufferDesc {
	enum Type {
		FIXED,
		RELATIVE,
		DISPLAY_SIZE
	};
	Type type = RELATIVE;
	IVec2 fixed_size;
	Vec2 rel_size = Vec2(1, 1);
	gpu::TextureFormat format;
	gpu::TextureFlags flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS;
	StaticString<32> debug_name;
};

struct LUMIX_RENDERER_API Pipeline {
	static UniquePtr<Pipeline> create(Renderer& renderer, PipelineType type);
	
	virtual ~Pipeline() {}

	virtual bool render(bool only_2d) = 0;
	virtual void render3DUI(EntityRef e, const struct Draw2D& drawdata, Vec2 canvas_size, bool orient_to_cam) = 0;
	virtual void setWorld(struct World* world) = 0;
	virtual RenderModule* getModule() const = 0;
	virtual Renderer& getRenderer() const = 0;
	virtual void setViewport(const Viewport& viewport) = 0;
	virtual const Viewport& getViewport() = 0;
	virtual const IVec2& getDisplaySize() const = 0;
	virtual void setIndirectLightMultiplier(float value) = 0;
	virtual void enablePixelJitter(bool enable) = 0;
	virtual void setClearColor(Vec3 color) = 0;

	virtual Draw2D& getDraw2D() = 0;
	virtual void clearDraw2D() = 0;
	virtual gpu::TextureHandle getOutput() = 0;

	virtual PipelineType getType() const = 0;
	virtual void copy(RenderBufferHandle dst, RenderBufferHandle src, IVec2 size, Vec4 r = Vec4(1, 0, 0, 0), Vec4 g = Vec4(0, 1, 0, 0), Vec4 b = Vec4(0, 0, 1, 0)) = 0;
	virtual void beginBlock(const char* name, bool stats = false) = 0;
	virtual void endBlock() = 0;
	virtual void drawArray(u32 indices_offset, u32 indices_count, Shader& shader, u32 define_mask = 0, gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FN_GREATER) = 0;
	virtual void setRenderTargets(Span<const RenderBufferHandle> renderbuffers, RenderBufferHandle ds = INVALID_RENDERBUFFER, gpu::FramebufferFlags flags = gpu::FramebufferFlags::NONE) = 0;
	virtual RenderBufferHandle createRenderbuffer(const RenderbufferDesc& desc) = 0;
	// renderbuffers are released every frame, this function should be called to keep them alive for the next frame
	virtual void keepRenderbufferAlive(RenderBufferHandle idx) = 0;
	virtual void clear(gpu::ClearFlags flags, float r, float g, float b, float a, float depth) = 0;
	// also emits barriers
	virtual gpu::BindlessHandle toBindless(RenderBufferHandle rb_idx, struct DrawStream& stream) = 0;
	virtual gpu::RWBindlessHandle toRWBindless(RenderBufferHandle rb_idx, DrawStream& stream) = 0;
	virtual RenderBufferHandle getDownscaledDepth(RenderBufferHandle depth_buffer) = 0;

	virtual void setUniformRaw(Span<const u8> mem, UniformBuffer::Enum bind_point = UniformBuffer::DRAWCALL) = 0;
	virtual void blit(gpu::BindlessHandle src, gpu::RWBindlessHandle dst, IVec2 size, bool flip_x = false, bool flip_y = false) = 0;
	virtual void viewport(i32 x, i32 y, i32 w, i32 h) = 0;
	virtual void pass(const CameraParams& cp) const = 0;
	virtual u32 cull(const CameraParams& cp, Span<const BucketDesc> buckets) = 0;
	virtual void renderBucket(u32 view_idx, u32 bucket_idx) const = 0;
	virtual void dispatch(Shader& shader, u32 x, u32 y, u32 z, const char* define = nullptr) = 0;
	virtual gpu::TextureHandle toTexture(RenderBufferHandle handle) = 0;

	// use this to store per pipeline data for e.g. postprocess effects
	// data are own by pipeline, destructor is not called
	// e.g. auto* data = pipeline.getData<TDAOPipelineData>();
	template <typename T>
	T* getData() {
		static_assert(__is_trivially_destructible(T), "Only trivially desctructible types are allowed");
		static u32 idx = s_data_type_generator++;
		auto [mem, first_use] = getData(idx, sizeof(T), alignof(T));
		if (first_use) new (NewPlaceholder(), mem) T();
		return (T*)mem;
	}

	template <typename T>
	void setUniform(const T& value, UniformBuffer::Enum bind_point = UniformBuffer::DRAWCALL) {
		Span<const u8> mem((const u8*)&value, sizeof(value));
		setUniformRaw(mem, bind_point);
	}

	enum class DebugShow : u32 {
		NONE,
		ALBEDO,
		NORMAL,
		ROUGHNESS,
		METALLIC,
		AO,
		LIGHT_CLUSTERS,
		PROBE_CLUSTERS,
		VELOCITY,

		BUILTIN_COUNT,
		PLUGIN,
	};

	DebugShow m_debug_show = DebugShow::NONE;
	struct RenderPlugin* m_debug_show_plugin = nullptr;

protected:
	static inline u32 s_data_type_generator = 0;

	struct InstanceData {
		void* memory;
		bool first_use; // if true, memory content is undefined, user should initialize it
	};
	virtual InstanceData getData(u32 idx, u32 size, u32 align) = 0;
};

} // namespace Lumix

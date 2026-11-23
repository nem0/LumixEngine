#pragma once

#include "engine/lumix.h"

#include "core/allocator.h"
#include "core/atomic.h"
#include "core/color.h"
#include "core/math.h"
#include "core/profiler.h"
#include "draw_stream.h"
#include "engine/plugin.h"
#include "gpu/gpu.h"
#include "vertex_decl.h"

#ifndef _WIN32
	#include "engine/engine.h"
#endif

namespace Lumix {

enum class AttributeSemantic : u8;
struct GBuffer;
struct ClusterStream;
struct GPUSceneRegistry;
enum class MaterialIndex : u32;
enum class PipelineType;

struct RenderbufferDesc {
	IVec2 size;
	gpu::TextureFormat format;
	gpu::TextureFlags flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS;
	const char* debug_name;
};

enum class RenderBufferHandle : u32 {};

static constexpr RenderBufferHandle INVALID_RENDERBUFFER = RenderBufferHandle{0xffFFffFF};

struct LUMIX_RENDERER_API RenderPlugin {
	virtual ~RenderPlugin() {}
	// all `RenderPlugin` functions are called during execution of every `Pipeline`
	virtual void renderUI(struct Pipeline& pipeline) {}
	virtual void renderOpaque(Pipeline& pipeline) {}
	virtual void renderTransparent(Pipeline& pipeline) {}
	virtual void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) {}
	virtual RenderBufferHandle renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline);
	virtual RenderBufferHandle renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline);
	virtual RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline);
	// called at the very end of every frame of every `Pipeline`, `output` is the final image (UI, film grain and 2D drawing are done), it can't be modified
	virtual void renderEnd(RenderBufferHandle output, Pipeline& pipeline) {}
	// returns valid buffer if plugin's Antialiasing run and no other AA should run
	virtual RenderBufferHandle renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline);
	// returns true if plugin's tonemap run and builtin tonemap should not run
	virtual bool tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline);
	
	virtual void debugUI(Pipeline&) {}
	virtual bool debugOutput(RenderBufferHandle input, Pipeline& pipeline);
	
	virtual void pipelineDestroyed(Pipeline& pipeline) {}
	virtual void frame(struct Renderer& renderer) {}
	virtual void shutdown(struct Renderer& renderer) {}
};

struct DrawStream;
struct UniformPool;
struct TransientPool;

//@ enum
enum class ScreenshotState : i32 {
	NONE, // nothing was requested yet
	PENDING, // waiting for the next game view frame
	SAVING, // the frame is being read back and written
	SAVED, // the file exists
	FAILED // see the log
};

//@ object
struct LUMIX_RENDERER_API Renderer : ISystem {
	struct MemRef {
		u32 size = 0;
		void* data = nullptr;
		bool own = false;
	};

	enum { MAX_SHADER_DEFINES = 32 };

	virtual void frame() = 0;
	virtual u32 frameNumber() const = 0;
	virtual void waitForRender() = 0;
	virtual void waitForCommandSetup() = 0;
	virtual void waitCanSetup() = 0;
	virtual struct Engine& getEngine() = 0;


	// Caller owns the pipeline and must destroy it before the renderer.
	//@ function
	virtual Pipeline& createPipeline(PipelineType type) = 0;
	//@ function
	virtual void destroyPipeline(Pipeline& pipeline) = 0;
	//@ function
	virtual float getLODMultiplier() const = 0;
	//@ function
	virtual void setLODMultiplier(float value) = 0;
	//@ function
	virtual bool isVSyncEnabled() const = 0;
	//@ function
	virtual void enableVsync(bool enable) = 0;

	// Saves the next rendered frame of a game view pipeline (with the in-game UI) as a PNG to an absolute path.
	// max_width and max_height, if not 0, limit the size of the image: it is averaged down by
	// a whole factor until it fits. Only one screenshot can be in progress.
	//@ function
	virtual bool requestScreenshot(StringView path, u32 max_width, u32 max_height) = 0;
	//@ function
	virtual ScreenshotState getScreenshotState() const = 0;
	// called by a game view pipeline after it rendered into texture, saves the screenshot if one was requested
	virtual void saveScreenshotIfRequested(gpu::TextureHandle texture, IVec2 size) = 0;
	// Reads back a texture (RGBA8) and writes it as a PNG to an absolute path, asynchronously. It is written to a temporary file first, so the file
	// exists only when complete. max_width and max_height, if not 0, limit the size of the image (averaged down by a whole factor).
	// If result is not null, it is set to ScreenshotState::SAVED or FAILED when done, so it must outlive the call.
	virtual void saveTexturePNG(StringView path, gpu::TextureHandle texture, IVec2 size, u32 max_width, u32 max_height, AtomicI32* result) = 0;
	
	virtual struct ArenaAllocator& getCurrentFrameAllocator() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual MemRef allocate(u32 size) = 0;
	virtual MemRef copy(const void* data, u32 size) = 0 ;
	virtual void free(const MemRef& memory) = 0;

	virtual void addPlugin(RenderPlugin& plugin) = 0;
	virtual void removePlugin(RenderPlugin& plugin) = 0;
	virtual Span<RenderPlugin*> getPlugins() = 0;
	// Runs the scripts of the frame (render.evox), the evox system gives it the script runtime.
	virtual struct EvoxRender& getRenderScript() = 0;

	virtual u8 getShaderDefineIdx(const char* define) = 0;
	virtual const char* getShaderDefine(int define_idx) const = 0;
	virtual int getShaderDefinesCount() const = 0;
	virtual u8 getLayerIdx(const char* name) = 0;
	virtual u8 getLayersCount() const = 0;
	virtual const char* getLayerName(u8 layer) const = 0;
	// bumped whenever a material or shader is unloaded (e.g. on reload), per-instance data derived from them is stale then
	virtual u32 getMaterialReloadCounter() const = 0;
	virtual void notifyMaterialReload() = 0;
	// Temporal antialiasing is a script (core:postprocess/taa) that does nothing when this is off. Upscalers such as DLSS
	// and FSR3 do their own antialiasing and turn it off.
	virtual void enableBuiltinTAA(bool enable) = 0;
	virtual bool isBuiltinTAAEnabled() const = 0;
	
	// Vertex shaders read vertices from a buffer, see vertex_pulling.hlsli. The defines have the layout of vertices
	// (stride, offset and format of each attribute).
	virtual const char* getSemanticDefines(Span<const AttributeSemantic> attributes, const VertexDecl& decl, u32 vertex_stride) = 0;

	virtual struct FontManager& getFontManager() = 0;
	virtual struct ResourceManager& getTextureManager() = 0;
	
	virtual MaterialIndex createMaterialConstants(Span<const float> data) = 0;
	virtual MaterialIndex createMaterialInstance(Span<const float> data) = 0;
	virtual void updateMaterialConstants(MaterialIndex handle, Span<const float> data, u32 offset) = 0;
	virtual void destroyMaterialConstants(MaterialIndex id) = 0;
	virtual gpu::BufferHandle getMaterialUniformBuffer() = 0;
	
	virtual TransientPool& getTransientPool() = 0;
	virtual UniformPool& getUniformPool() = 0;
	
	virtual gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags, const char* debug_name) = 0;

	// The vertex data, clusters and nodes of all meshes with clusters (see Mesh::cluster_vertex_offset) are ranges of one buffer of a fixed size, created by the first allocation.
	// `allocClusterData` copies `size` bytes of `data` to a new range and returns its byte offset, or false if the buffer is full. A freed range is reused
	// after the frames in flight that may read it are done. Can be called from any thread.
	virtual bool allocClusterData(const void* data, u32 size, u32& offset) = 0;
	// The same range of the buffer with no data: the caller copies to it (GPUSceneRegistry does it for the meshes without clusters, see flushGPUSceneRegistry). The offset is a multiple of
	// `alignment` (any positive number), the size is not rounded, free it with freeClusterData.
	// If there is no room and `reclaim` is set, the renderer unloads streamed pages (see ClusterStream) and tries again, so it can fail only if the data that is never unloaded does not fit.
	virtual bool allocClusterRange(u32 size, u32 alignment, u32& offset, bool reclaim = true) = 0;
	// `immediate`: the range can be allocated again at once, not after the frames in flight (only for data that the GPU is not going to use any more, ClusterStream::evictPage)
	virtual void freeClusterData(u32 offset, u32 size, bool immediate = false) = 0;
	virtual gpu::BufferHandle getClusterDataBuffer() = 0; // invalid until the first allocation
	// The scratch buffer of the culling of the trees of clusters (nanite.hlsl): the lists of nodes of its groups. What is in it is dead when a dispatch ends, so all the pipelines and views use
	// this one buffer: the draw streams are executed one after another and every use has its barriers. Made by the first call, which must be made when the frame is set up (the creation is
	// recorded in the draw stream of the frame).
	virtual gpu::BufferHandle getClusterScratchBuffer() = 0;
	virtual u32 getClusterScratchCapacity() const = 0;	// u32 in it
	// What the GPU scenes of all worlds share (models, meshes, material sets, draw groups), see gpu_scene_registry.h. Made by the first call.
	virtual GPUSceneRegistry& getGPUSceneRegistry() = 0;
	// Copies `size` bytes (a multiple of 4) to the bytes of a range of the buffer that was allocated before (the copy is recorded in the draw stream of the frame, so the GPU executes
	// it before the passes that are recorded later).
	virtual void updateClusterData(u32 offset, const void* data, u32 size) = 0;

	// Streaming of the payload of clusters, see ClusterStream. Ids are for the pages of a mesh in the feedback buffer, one bit for each: set if a cluster of a group of the page was drawn (the page is loaded) or if
	// the clusters that a group of the page is refined to were needed (it is not loaded), since the last readback. `allocClusterStreamIds` returns ClusterStream::NONE if there
	// are not enough ids. Streams are registered when they are complete, unregistering frees the ids. All can be called from any thread.
	virtual u32 allocClusterStreamIds(u32 count) = 0;
	virtual void registerClusterStream(ClusterStream* stream) = 0;
	virtual void unregisterClusterStream(ClusterStream* stream) = 0;
	virtual bool isClusterStreamingEnabled() const = 0;	// false: everything is loaded with the model (-cluster_stream_off)
	// The result of Model::requestClusterPage, called from the main thread when the file system is done (`data` is valid only during the call). `stream` is null if the model was
	// unloaded meanwhile. `generation` is ClusterStream::generation at the time of the request, the read is ignored if the stream at the address is another one (it was freed and
	// the address was reused). The payload is kept until the page is uploaded, which waits for the pages that it needs.
	virtual void onClusterPageRead(ClusterStream* stream, u64 generation, u32 page, Span<const u8> data, bool success) = 0;
	virtual gpu::BufferHandle getClusterFeedbackBuffer() = 0;	// invalid until the first stream is registered
	virtual u32 getClusterFeedbackCapacity() = 0;			// groups in it

	virtual gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) = 0;

	virtual RenderBufferHandle createRenderbuffer(const RenderbufferDesc& desc) = 0;
	virtual void releaseRenderbuffer(RenderBufferHandle idx) = 0;
	virtual void setRenderTargets(Span<const RenderBufferHandle> renderbuffers, RenderBufferHandle ds = INVALID_RENDERBUFFER, gpu::FramebufferFlags flags = gpu::FramebufferFlags::NONE) = 0;
	virtual gpu::TextureHandle toTexture(RenderBufferHandle handle) = 0;

	virtual gpu::ProgramHandle queueShaderCompile(struct Shader& shader, const struct ShaderKey& key) = 0;
	virtual DrawStream& getDrawStream() = 0;
	virtual DrawStream& getEndFrameDrawStream() = 0;

	template <typename T> void pushJob(const char* name, const T& func);

	virtual void beginProfileBlock(const char* name, i64 link, bool stats = false) = 0;
	virtual void endProfileBlock() = 0;

protected:
	virtual void setupJob(void* user_ptr, void(*task)(void*)) = 0;
}; 

struct UniformPool;
struct TransientPool;

LUMIX_RENDERER_API TransientSlice alloc(UniformPool& pool, u32 size);
LUMIX_RENDERER_API TransientSlice alloc(UniformPool& pool, const void* data, u32 size);
LUMIX_RENDERER_API TransientSlice alloc(TransientPool& pool, u32 size);

template <typename T>
void Renderer::pushJob(const char* name, const T& func) {
	struct Context {
		Context(DrawStream& stream, T func, const char* name) 
			: stream(stream)
			, func(func)
			, name(name)
		{}

		static void run(void* ptr) {
			Context* that = (Context*)ptr;
			if (that->name) {
				profiler::beginBlock(that->name);
				const i64 link = profiler::createNewLinkID();
				profiler::link(link);
				profiler::blockColor(Color(0x7f, 0, 0x7f, 0xff).abgr());
				that->stream.beginProfileBlock(that->name, link, false);
			}
			that->func(that->stream);
			if (that->name) {
				that->stream.endProfileBlock();
				profiler::endBlock();
			}
			that->~Context();
		}

		DrawStream& stream;
		T func;
		const char* name;
	};
	
	DrawStream& parent = getDrawStream();
	u8* mem = parent.userAlloc(sizeof(Context));
	DrawStream& stream = parent.createSubstream();
	Context* ctx = new (NewPlaceholder(), mem) Context(stream, func, name);
	setupJob(ctx, &Context::run);
}

} // namespace Lumix


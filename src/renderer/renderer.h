#pragma once

#include "engine/allocator.h"
#include "engine/lumix.h"
#include "engine/plugin.h"
#include "gpu/gpu.h"

namespace Lumix {

struct RenderPlugin {
	virtual void renderUI(struct Pipeline& pipeline) {}
	virtual void renderOpaque(Pipeline& pipeline) {}
	virtual void renderTransparent(Pipeline& pipeline) {}
};

struct LUMIX_RENDERER_API Renderer : IPlugin {
	struct MemRef {
		u32 size = 0;
		void* data = nullptr;
		bool own = false;
	};

	struct RenderJob {
		RenderJob() {}
		RenderJob(const RenderJob& rhs) = delete;

		virtual ~RenderJob() {}
		virtual void setup() = 0;
		virtual void execute() = 0;
		i64 profiler_link = 0;
	};

	struct TransientSlice {
		gpu::BufferHandle buffer;
		u32 offset;
		u32 size;
		u8* ptr;
	};

	enum { 
		MAX_SHADER_DEFINES = 32,
		SCRATCH_BUFFER_SIZE = 1024 * 1024 * 2
	};

	virtual void startCapture() = 0;
	virtual void stopCapture() = 0;
	virtual void frame() = 0;
	virtual void waitForRender() = 0;
	virtual void waitForCommandSetup() = 0;
	virtual void makeScreenshot(const struct Path& filename) = 0;
	virtual u8 getShaderDefineIdx(const char* define) = 0;
	virtual const char* getShaderDefine(int define_idx) const = 0;
	virtual int getShaderDefinesCount() const = 0;
	virtual gpu::ProgramHandle queueShaderCompile(struct Shader& shader, gpu::VertexDecl decl, u32 defines) = 0;
	virtual struct FontManager& getFontManager() = 0;
	virtual struct ResourceManager& getTextureManager() = 0;
	virtual void addPlugin(RenderPlugin& plugin) = 0;
	virtual void removePlugin(RenderPlugin& plugin) = 0;
	virtual Span<RenderPlugin*> getPlugins() = 0;
	
	virtual u32 createMaterialConstants(const struct MaterialConsts& data) = 0;
	virtual void destroyMaterialConstants(u32 id) = 0;
	virtual gpu::BufferHandle getMaterialUniformBuffer() = 0;

	virtual IAllocator& getAllocator() = 0;
	virtual MemRef allocate(u32 size) = 0;
	virtual MemRef copy(const void* data, u32 size) = 0 ;
	virtual void free(const MemRef& memory) = 0;
	
	virtual gpu::BufferHandle getScratchBuffer() = 0;
	virtual TransientSlice allocTransient(u32 size) = 0;
	virtual gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags) = 0;
	virtual void destroy(gpu::BufferHandle buffer) = 0;
	virtual void destroy(gpu::ProgramHandle program) = 0;
	
	virtual gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) = 0;
	virtual gpu::TextureHandle loadTexture(const gpu::TextureDesc& desc, const MemRef& image_data, gpu::TextureFlags flags, const char* debug_name) = 0;
	virtual void copy(gpu::TextureHandle dst, gpu::TextureHandle src) = 0;
	virtual void downscale(gpu::TextureHandle src, u32 src_w, u32 src_h, gpu::TextureHandle dst, u32 dst_w, u32 dst_h) = 0;
	virtual void updateTexture(gpu::TextureHandle handle, u32 slice, u32 x, u32 y, u32 w, u32 h, gpu::TextureFormat format, const MemRef& memory) = 0;
	virtual void getTextureImage(gpu::TextureHandle texture, u32 w, u32 h, gpu::TextureFormat out_format, Span<u8> data) = 0;
	virtual void destroy(gpu::TextureHandle tex) = 0;
	
	virtual void queue(RenderJob& cmd, i64 profiler_link) = 0;

	virtual void beginProfileBlock(const char* name, i64 link) = 0;
	virtual void endProfileBlock() = 0;
	virtual void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) = 0;

	virtual u32 allocSortKey(struct Mesh* mesh) = 0;
	virtual void freeSortKey(u32 key) = 0;
	virtual u32 getMaxSortKey() const = 0;
	virtual const Mesh** getSortKeyToMeshMap() const = 0;

	virtual u8 getLayerIdx(const char* name) = 0;
	virtual u8 getLayersCount() const = 0;
	virtual const char* getLayerName(u8 layer) const = 0;

	virtual struct Engine& getEngine() = 0;

	template <typename T, typename... Args> T& createJob(Args&&... args) {
		return *new (NewPlaceholder(), allocJob(sizeof(T), alignof(T))) T(static_cast<Args&&>(args)...);
	}

	template <typename T> void destroyJob(T& job) {
		job.~T();
		deallocJob(&job);
	}

protected:
	virtual void* allocJob(u32 size, u32 align) = 0;
	virtual void deallocJob(void* ptr) = 0;
}; 


} // namespace Lumix


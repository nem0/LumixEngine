#pragma once

#include "engine/allocator.h"
#include "engine/lumix.h"
#include "engine/plugin.h"
#include "gpu/gpu.h"

#ifndef _WIN32
	#include "draw_stream.h"
	#include "engine/engine.h"
	#include "engine/profiler.h"
#endif

namespace Lumix {

struct RenderPlugin {
	virtual void renderUI(struct Pipeline& pipeline) {}
	virtual void renderOpaque(Pipeline& pipeline) {}
	virtual void renderTransparent(Pipeline& pipeline) {}
};

struct DrawStream;

struct LUMIX_RENDERER_API Renderer : IPlugin {
	struct MemRef {
		u32 size = 0;
		void* data = nullptr;
		bool own = false;
	};

	struct TransientSlice {
		gpu::BufferHandle buffer;
		u32 offset;
		u32 size;
		u8* ptr;
	};

	enum { 
		MAX_SHADER_DEFINES = 32,
	};

	virtual void frame() = 0;
	virtual u32 frameNumber() const = 0;
	virtual void waitForRender() = 0;
	virtual void waitForCommandSetup() = 0;
	virtual void waitCanSetup() = 0;
	virtual void makeScreenshot(const struct Path& filename) = 0;
	virtual u8 getShaderDefineIdx(const char* define) = 0;
	virtual const char* getShaderDefine(int define_idx) const = 0;
	virtual int getShaderDefinesCount() const = 0;
	virtual gpu::ProgramHandle queueShaderCompile(struct Shader& shader, gpu::StateFlags state, gpu::VertexDecl decl, u32 defines) = 0;
	virtual struct FontManager& getFontManager() = 0;
	virtual struct ResourceManager& getTextureManager() = 0;
	virtual void addPlugin(RenderPlugin& plugin) = 0;
	virtual void removePlugin(RenderPlugin& plugin) = 0;
	virtual Span<RenderPlugin*> getPlugins() = 0;
	
	virtual float getLODMultiplier() const = 0;
	virtual void setLODMultiplier(float value) = 0;

	virtual u32 createMaterialConstants(Span<const float> data) = 0;
	virtual void destroyMaterialConstants(u32 id) = 0;
	virtual gpu::BufferHandle getMaterialUniformBuffer() = 0;

	virtual IAllocator& getAllocator() = 0;
	virtual MemRef allocate(u32 size) = 0;
	virtual MemRef copy(const void* data, u32 size) = 0 ;
	virtual void free(const MemRef& memory) = 0;
	
	virtual TransientSlice allocTransient(u32 size) = 0;
	virtual TransientSlice allocUniform(u32 size) = 0;
	virtual TransientSlice allocUniform(const void* data, u32 size) = 0;
	virtual gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags) = 0;
	
	virtual void destroy(gpu::BufferHandle buffer) = 0;
	virtual void destroy(gpu::ProgramHandle program) = 0;
	virtual void destroy(gpu::TextureHandle tex) = 0;
	
	virtual gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) = 0;
	
	virtual void beginProfileBlock(const char* name, i64 link, bool stats = false) = 0;
	virtual void endProfileBlock() = 0;

	virtual u32 allocSortKey(struct Mesh* mesh) = 0;
	virtual void freeSortKey(u32 key) = 0;
	virtual u32 getMaxSortKey() const = 0;
	virtual const Mesh** getSortKeyToMeshMap() const = 0;

	virtual u8 getLayerIdx(const char* name) = 0;
	virtual u8 getLayersCount() const = 0;
	virtual const char* getLayerName(u8 layer) const = 0;

	virtual struct Engine& getEngine() = 0;

	virtual DrawStream& getDrawStream() = 0;

	template <typename T> void pushJob(const char* name, const T& func);
	template <typename T> void pushJob(const T& func) { pushJob(nullptr, func); }
	template <typename T, typename... Args> T& createJob(Args&&... args);

	virtual struct LinearAllocator& getCurrentFrameAllocator() = 0;

protected:
	virtual void setupJob(void* user_ptr, void(*task)(void*)) = 0;
}; 


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
				profiler::blockColor(0x7f, 0, 0x7f);
				that->stream.beginProfileBlock(that->name);
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
	
	DrawStream& stream = getDrawStream().createSubstream();
	Context* ctx = new (NewPlaceholder(), stream.userAlloc(sizeof(Context))) Context(stream, func, name);
	setupJob(ctx, &Context::run);
}

} // namespace Lumix


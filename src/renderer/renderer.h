#pragma once

#include "engine/lumix.h"
#include "engine/plugin.h"
#include "engine/math.h"
#include "engine/string.h"
#include "gpu/gpu.h"


namespace Lumix
{


struct Engine;
struct FontManager;
struct MaterialConsts;
struct Path;
struct Pipeline;
struct ResourceManager;
struct TextureManager;


struct LUMIX_RENDERER_API Renderer : IPlugin 
{
	struct MemRef
	{
		u32 size = 0;
		void* data = nullptr;
		bool own = false;
	};

	struct RenderJob
	{
		virtual ~RenderJob() {}
		virtual void setup() = 0;
		virtual void execute() = 0;
		i64 profiler_link = 0;
	};

	struct TransientSlice
	{
		gpu::BufferHandle buffer;
		u32 offset;
		u32 size;
		u8* ptr;
	};

	enum { MAX_SHADER_DEFINES = 32 };

	virtual void startCapture() = 0;
	virtual void stopCapture() = 0;
	virtual void frame() = 0;
	virtual void waitForRender() = 0;
	virtual void waitForCommandSetup() = 0;
	virtual void makeScreenshot(const Path& filename) = 0;
	virtual u8 getShaderDefineIdx(const char* define) = 0;
	virtual const char* getShaderDefine(int define_idx) const = 0;
	virtual int getShaderDefinesCount() const = 0;
	virtual gpu::ProgramHandle queueShaderCompile(struct Shader& shader, gpu::VertexDecl decl, u32 defines) = 0;
	virtual FontManager& getFontManager() = 0;
	virtual ResourceManager& getTextureManager() = 0;
	
	virtual u32 createMaterialConstants(const MaterialConsts& data) = 0;
	virtual void destroyMaterialConstants(u32 id) = 0;
	virtual gpu::BufferGroupHandle getMaterialUniformBuffer() = 0;

	virtual IAllocator& getAllocator() = 0;
	virtual MemRef allocate(u32 size) = 0;
	virtual MemRef copy(const void* data, u32 size) = 0 ;
	virtual void free(const MemRef& memory) = 0;
	
	virtual TransientSlice allocTransient(u32 size) = 0;
	virtual gpu::BufferHandle createBuffer(const MemRef& memory, u32 flags) = 0;
	virtual void destroy(gpu::BufferHandle buffer) = 0;
	virtual void destroy(gpu::ProgramHandle program) = 0;
	
	virtual gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, u32 flags, const MemRef& memory, const char* debug_name) = 0;
	virtual gpu::TextureHandle loadTexture(const MemRef& memory, u32 flags, gpu::TextureInfo* info, const char* debug_name) = 0;
	virtual void updateTexture(gpu::TextureHandle handle, u32 x, u32 y, u32 w, u32 h, gpu::TextureFormat format, const MemRef& memory) = 0;
	virtual void getTextureImage(gpu::TextureHandle texture, u32 w, u32 h, gpu::TextureFormat out_format, Span<u8> data) = 0;
	virtual void destroy(gpu::TextureHandle tex) = 0;
	
	virtual void queue(RenderJob* cmd, i64 profiler_link) = 0;

	virtual void beginProfileBlock(const char* name, i64 link) = 0;
	virtual void endProfileBlock() = 0;
	virtual void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) = 0;

	virtual u8 getLayerIdx(const char* name) = 0;
	virtual u8 getLayersCount() const = 0;
	virtual const char* getLayerName(u8 layer) const = 0;

	virtual Engine& getEngine() = 0;
}; 


} // namespace Lumix


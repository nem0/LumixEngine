#pragma once

#include "engine/lumix.h"
#include "engine/iplugin.h"
#include "engine/math.h"
#include "engine/string.h"
#include "ffr/ffr.h"


namespace Lumix
{


class Engine;
class FontManager;
class Path;
class Pipeline;
class ResourceManager;
class TextureManager;


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
	public:
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
			ffr::BufferHandle buffer;
			u32 offset;
			u32 size;
			u8* ptr;
		};

		enum { MAX_SHADER_DEFINES = 32 };
	public:
		virtual ~Renderer() {}
		virtual void startCapture() = 0;
		virtual void stopCapture() = 0;
		virtual void frame() = 0;
		virtual void resize(int width, int height) = 0;
		virtual void makeScreenshot(const Path& filename) = 0;
		virtual u8 getShaderDefineIdx(const char* define) = 0;
		virtual const char* getShaderDefine(int define_idx) const = 0;
		virtual int getShaderDefinesCount() const = 0;
		virtual FontManager& getFontManager() = 0;
		virtual ResourceManager& getTextureManager() = 0;
		
		virtual IAllocator& getAllocator() = 0;
		virtual MemRef allocate(u32 size) = 0;
		virtual MemRef copy(const void* data, u32 size) = 0 ;
		virtual void free(const MemRef& memory) = 0;
		
		virtual TransientSlice allocTransient(u32 size) = 0;
		virtual ffr::BufferHandle createBuffer(const MemRef& memory) = 0;
		virtual ffr::VAOHandle createVAO(const ffr::VertexAttrib* attribs, u32 attribs_count) = 0;
		virtual void destroy(ffr::BufferHandle buffer) = 0;
		virtual void destroy(ffr::VAOHandle buffer) = 0;
		virtual void destroy(ffr::ProgramHandle program) = 0;
		
		virtual ffr::TextureHandle createTexture(u32 w, u32 h, u32 depth, ffr::TextureFormat format, u32 flags, const MemRef& memory, const char* debug_name) = 0;
		virtual ffr::TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info, const char* debug_name) = 0;
		virtual void updateTexture(ffr::TextureHandle handle, u32 x, u32 y, u32 w, u32 h, ffr::TextureFormat format, const MemRef& memory) = 0;
		virtual void getTextureImage(ffr::TextureHandle texture, int size, void* data) = 0;
		virtual void destroy(ffr::TextureHandle tex) = 0;
		
		virtual void queue(RenderJob* cmd, i64 profiler_link) = 0;
		virtual ffr::FramebufferHandle getFramebuffer() const = 0;

		virtual void beginProfileBlock(const char* name, i64 link) = 0;
		virtual void endProfileBlock() = 0;
		virtual void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) = 0;

		virtual u8 getLayerIdx(const char* name) = 0;
		virtual u8 getLayersCount() const = 0;
		virtual const char* getLayerName(u8 layer) const = 0;

		virtual Engine& getEngine() = 0;
}; 


} // namespace Lumix


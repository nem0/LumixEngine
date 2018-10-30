#pragma once

#include "engine/lumix.h"
#include "engine/iplugin.h"
#include "engine/matrix.h"
#include "engine/string.h"
#include "ffr/ffr.h"


namespace Lumix
{


class Engine;
struct Font;
struct FontAtlas;
class FontManager;
class LIFOAllocator;
class MaterialManager;
class ModelManager;
class Path;
class Pipeline;
class Shader;
class ShaderManager;
class TextureManager;
template <typename T> class Array;


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
	public:
		struct MemRef
		{
			uint size = 0;
			void* data = nullptr;
			bool own = false;
		};

		struct RenderJob
		{
			virtual ~RenderJob() {}
			virtual void setup() = 0;
			virtual void execute() = 0;
		};

		struct GlobalState 
		{
			Matrix shadow_view_projection;
			Matrix shadowmap_matrices[4];
			Matrix camera_projection;
			Matrix camera_inv_projection;
			Matrix camera_view;
			Matrix camera_inv_view;
			Matrix camera_view_projection;
			Matrix camera_inv_view_projection;
			Vec4 light_direction;
			Vec3 light_color;
			float light_intensity;
			float light_indirect_intensity;
			float time;
			IVec2 framebuffer_size;
		};

		struct GPUProfilerQuery
		{
			StaticString<32> name;
			ffr::QueryHandle handle;
			u64 result;
			bool is_end;
		};

		struct TransientSlice
		{
			ffr::BufferHandle buffer;
			uint offset;
			uint size;
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
		virtual MaterialManager& getMaterialManager() = 0;
		virtual ShaderManager& getShaderManager() = 0;
		virtual ModelManager& getModelManager() = 0;
		virtual TextureManager& getTextureManager() = 0;
		virtual void setMainPipeline(Pipeline* pipeline) = 0;
		virtual Pipeline* getMainPipeline() = 0;
		virtual GlobalState getGlobalState() const = 0;
		virtual void setGlobalState(const GlobalState& state) = 0;
		
		virtual IAllocator& getAllocator() = 0;
		virtual MemRef allocate(uint size) = 0;
		virtual MemRef copy(const void* data, uint size) = 0 ;
		virtual void free(const MemRef& memory) = 0;
		
		virtual TransientSlice allocTransient(uint size) = 0;
		virtual ffr::BufferHandle createBuffer(const MemRef& memory) = 0;
		virtual void destroy(ffr::BufferHandle buffer) = 0;
		
		virtual void destroy(ffr::ProgramHandle program) = 0;
		
		virtual ffr::TextureHandle createTexture(uint w, uint h, uint depth, ffr::TextureFormat format, u32 flags, const MemRef& memory, const char* debug_name) = 0;
		virtual ffr::TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info, const char* debug_name) = 0;
		virtual void getTextureImage(ffr::TextureHandle texture, int size, void* data) = 0;
		virtual void destroy(ffr::TextureHandle tex) = 0;
		
		virtual void push(RenderJob* cmd) = 0;
		virtual ffr::FramebufferHandle getFramebuffer() const = 0;

		virtual bool getGPUTimings(Array<GPUProfilerQuery>* results) = 0;
		virtual void beginProfileBlock(const char* name) = 0;
		virtual void endProfileBlock() = 0;
		virtual void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) = 0;

		virtual u8 getLayerIdx(const char* name) = 0;
		virtual u8 getLayersCount() const = 0;
		virtual const char* getLayerName(u8 layer) const = 0;

		virtual Engine& getEngine() = 0;
}; 


} // namespace Lumix


#pragma once

#include "engine/lumix.h"
#include "engine/iplugin.h"
#include "engine/matrix.h"
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


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
	public:
		struct MemRef
		{
			uint size = 0;
			void* data = nullptr;
			bool own = false;
		};

		struct RenderCommandBase
		{
			virtual ~RenderCommandBase() {}
			virtual MemRef setup() = 0;
			virtual void execute(const MemRef& user_ptr) = 0;
			virtual const char* getName() const = 0;
		};

		struct GlobalState 
		{
			Matrix shadow_view_projection;
			Matrix shadowmap_matrices[4];
			Matrix camera_projection;
			Matrix camera_view;
			Matrix camera_view_projection;
			Matrix camera_inv_view_projection;
			Vec4 camera_pos;
			Vec4 light_direction;
			Vec3 light_color;
			float light_intensity;
			float light_indirect_intensity;
			Int2 framebuffer_size;
		};

		enum { MAX_SHADER_DEFINES = 32 };
	public:
		virtual ~Renderer() {}
		virtual void frame(bool capture) = 0;
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
		virtual Shader* getDefaultShader() = 0;
		virtual int getLayersCount() const = 0;
		virtual int getLayer(const char* name) = 0;
		virtual const char* getLayerName(int idx) const = 0;
		virtual void setMainPipeline(Pipeline* pipeline) = 0;
		virtual Pipeline* getMainPipeline() = 0;
		virtual void setGlobalState(const GlobalState& state) = 0;
		
		virtual IAllocator& getAllocator() = 0;
		virtual MemRef allocate(uint size) = 0;
		virtual MemRef copy(const void* data, uint size) = 0 ;
		virtual void free(const MemRef& memory) = 0;
		
		virtual ffr::BufferHandle createBuffer(const MemRef& memory) = 0;
		virtual void destroy(ffr::BufferHandle buffer) = 0;
		
		virtual ffr::TextureHandle createTexture(uint w, uint h, ffr::TextureFormat format, u32 flags, const MemRef& memory) = 0;
		virtual ffr::TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info) = 0;
		virtual void destroy(ffr::TextureHandle tex) = 0;
		
		virtual void push(RenderCommandBase* cmd) = 0;
		virtual ffr::FramebufferHandle getFramebuffer() const = 0;

		virtual Engine& getEngine() = 0;
}; 


} // namespace Lumix


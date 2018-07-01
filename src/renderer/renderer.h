#pragma once

#include "engine/lumix.h"
#include "engine/iplugin.h"


namespace Lumix
{


namespace ffr { struct BufferHandle; }

class Engine;
struct Font;
struct FontAtlas;
class FontManager;
class GlobalStateUniforms;
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
		virtual GlobalStateUniforms& getGlobalStateUniforms() = 0;

		virtual Engine& getEngine() = 0;
}; 


} // namespace Lumix


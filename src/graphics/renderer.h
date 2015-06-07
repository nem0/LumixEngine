#pragma once

#include "core/lumix.h"
#include "core/array.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "graphics/render_scene.h"


namespace Lumix
{


class Engine;
class Geometry;
class IRenderDevice;
class Material;
class Model;
class Pipeline;
class PipelineInstance;
class Pose;
class ResourceManager;
class Shader;
class Texture;
class Universe;
struct VertexDef;


class LUMIX_ENGINE_API Renderer : public IPlugin 
{
	public:
		static Renderer* createInstance(Engine& engine);
		static void destroyInstance(Renderer& renderer);

		static void frame();
		static void init(void* data);

		virtual void makeScreenshot(const Path& filename, int width, int height) = 0;

		virtual Engine& getEngine() = 0;
}; 


} // !namespace Lumix


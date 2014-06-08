#pragma once

#include "core/lux.h"
#include "core/array.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "graphics/render_scene.h"


namespace Lux
{


class Engine;
class IRenderDevice;
class Material;
class Model;
class ModelInstance;
class Pipeline;
class PipelineInstance;
class Pose;
class ResourceManager;
class Shader;
class Texture;
class Universe;



class LUX_ENGINE_API Renderer : public IPlugin 
{
	public:
		static Renderer* createInstance();
		static void destroyInstance(Renderer& renderer);

		virtual void render(IRenderDevice& device) = 0;
		virtual void renderGame() = 0;
		virtual void enableZTest(bool enable) = 0;
		virtual void setRenderDevice(IRenderDevice& device) = 0;

		virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) = 0;
		virtual Engine& getEngine() = 0;
		
		/// "immediate mode"
		virtual void renderModel(const Model& model, const Matrix& transform, PipelineInstance& pipeline) = 0;
}; 


} // !namespace Lux


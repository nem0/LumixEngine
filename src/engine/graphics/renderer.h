#pragma once

#include "lumix.h"
#include "core/array.h"
#include "core/string.h"
#include "iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "graphics/render_scene.h"


namespace bgfx
{
	struct VertexDecl;
	struct TransientVertexBuffer;
	struct TransientIndexBuffer;
}


namespace Lumix
{


class Engine;
class Geometry;
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


void bgfx_allocTransientVertexBuffer(bgfx::TransientVertexBuffer* tvb, uint32_t num, const bgfx::VertexDecl& decl);
void bgfx_allocTransientIndexBuffer(bgfx::TransientIndexBuffer* tib, uint32_t num);
void bgfx_setState(uint64_t state, uint32_t rgba = 0);
uint16_t bgfx_setScissor(uint16_t x, uint16_t y, uint16_t width, uint16_t height);


class LUMIX_ENGINE_API Renderer : public IPlugin 
{
	public:
		typedef void* TransientDataHandle;

	public:
		static Renderer* createInstance(Engine& engine, void* init_data);
		static void destroyInstance(Renderer& renderer);

		virtual void frame() = 0;
		virtual int getViewCounter() const = 0;
		virtual void viewCounterAdd() = 0;
		virtual void makeScreenshot(const Path& filename) = 0;
		virtual int getPassIdx(const char* pass) = 0;

		virtual Engine& getEngine() = 0;
}; 


} // !namespace Lumix


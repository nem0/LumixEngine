#pragma once

#include "lumix.h"
#include "core/array.h"
#include "iplugin.h"
#include "renderer/ray_cast_model_hit.h"
#include "renderer/render_scene.h"


namespace bgfx
{
	struct VertexDecl;
	struct TransientVertexBuffer;
	struct TransientIndexBuffer;
}


namespace Lumix
{


class Engine;


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
	public:
		typedef void* TransientDataHandle;

	public:
		static void setInitData(void* data);

		virtual void frame() = 0;
		virtual void resize(int width, int height) = 0;
		virtual int getViewCounter() const = 0;
		virtual void viewCounterAdd() = 0;
		virtual void makeScreenshot(const Path& filename) = 0;
		virtual int getPassIdx(const char* pass) = 0;
		virtual int getShaderDefineIdx(const char* define) = 0;
		virtual const char* getShaderDefine(int define_idx) = 0;
		virtual LIFOAllocator& getFrameAllocator() = 0;
		virtual const bgfx::VertexDecl& getBasicVertexDecl() const = 0;
		virtual const bgfx::VertexDecl& getBasic2DVertexDecl() const = 0;

		virtual Engine& getEngine() = 0;
}; 


} // !namespace Lumix


#pragma once

#include "lumix.h"
#include "iplugin.h"


namespace bgfx
{
	struct VertexDecl;
}


namespace Lumix
{


class Engine;
class LIFOAllocator;
class MaterialManager;
class Path;
class Shader;


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
	public:
		typedef void* TransientDataHandle;

	public:
		virtual void frame() = 0;
		virtual void resize(int width, int height) = 0;
		virtual int getViewCounter() const = 0;
		virtual void viewCounterAdd() = 0;
		virtual void makeScreenshot(const Path& filename) = 0;
		virtual int getPassIdx(const char* pass) = 0;
		virtual uint8 getShaderDefineIdx(const char* define) = 0;
		virtual const char* getShaderDefine(int define_idx) = 0;
		virtual LIFOAllocator& getFrameAllocator() = 0;
		virtual const bgfx::VertexDecl& getBasicVertexDecl() const = 0;
		virtual const bgfx::VertexDecl& getBasic2DVertexDecl() const = 0;
		virtual MaterialManager& getMaterialManager() = 0;
		virtual Shader* getDefaultShader() = 0;

		virtual Engine& getEngine() = 0;
}; 


} // !namespace Lumix


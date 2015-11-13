#pragma once

#include "lumix.h"
#include "core/delegate.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
	
class FrameBuffer;
class JsonSerializer;
class Material;
struct Matrix;
class Model;
class Renderer;
class RenderScene;
class Texture;
class TransientGeometry;


namespace FS
{

class FileSystem;
class IFile;

}


class LUMIX_RENDERER_API PipelineManager : public ResourceManagerBase
{
public:
	PipelineManager(Renderer& renderer, IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_renderer(renderer)
		, m_allocator(allocator)
	{}
	~PipelineManager() {}
	Renderer& getRenderer() { return m_renderer; }

protected:
	virtual Resource* createResource(const Path& path) override;
	virtual void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
	Renderer& m_renderer;
};


class LUMIX_RENDERER_API Pipeline : public Resource
{
	public:
		Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		virtual ~Pipeline() {}
};



class LUMIX_RENDERER_API PipelineInstance
{
	public:
		typedef Delegate<void> CustomCommandHandler;

	public:
		virtual ~PipelineInstance() {}

		virtual void render() = 0;
		virtual void setViewport(int x, int y, int width, int height) = 0;

		static PipelineInstance* create(Pipeline& src, IAllocator& allocator);
		static void destroy(PipelineInstance* pipeline);

		virtual FrameBuffer* getFramebuffer(const char* framebuffer_name) = 0;
		virtual void setScene(RenderScene* scene) = 0;
		virtual RenderScene* getScene() = 0;
		virtual int getWidth() = 0;
		virtual int getHeight() = 0;
		virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
		virtual void setViewProjection(const Matrix& mtx, int width, int height) = 0;
		virtual void setScissor(int x, int y, int width, int height) = 0;
		virtual void setTexture(int slot,
			bgfx::TextureHandle texture,
			bgfx::UniformHandle uniform) = 0;
		virtual void render(TransientGeometry& geom,
			const Matrix& mtx,
			int first_index,
			int num_indices,
			uint64 render_states,
			bgfx::ProgramHandle program_handle) = 0;
		virtual void setWireframe(bool wireframe) = 0;
		virtual void renderModel(Model& model, const Matrix& mtx) = 0;
		virtual void toggleStats() = 0;
		virtual void setWindowHandle(void* data) = 0;
		virtual int getPassIdx() const = 0;
		virtual int getParameterCount() const = 0;
		virtual const char* getParameterName(int index) const = 0;
		virtual void setParameter(int index, bool value) = 0;
		virtual bool getParameter(int index) = 0;
};
}
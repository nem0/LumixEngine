#pragma once

#include "lumix.h"
#include "core/delegate.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"

namespace Lumix
{
	
class FrameBuffer;
class JsonSerializer;
struct Matrix;
class Model;
class Renderer;
class RenderScene;


namespace FS
{

class FileSystem;
class IFile;

}


class LUMIX_ENGINE_API PipelineManager : public ResourceManagerBase
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


class LUMIX_ENGINE_API Pipeline : public Resource
{
	public:
		Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		virtual ~Pipeline() {}
};



class LUMIX_ENGINE_API PipelineInstance abstract
{
	public:
		typedef Delegate<void> CustomCommandHandler; 

	public:
		virtual ~PipelineInstance() {}

		virtual void render() = 0;
		virtual void resize(int w, int h) = 0;

		static PipelineInstance* create(Pipeline& src, IAllocator& allocator);
		static void destroy(PipelineInstance* pipeline);

		virtual void setScene(RenderScene* scene) = 0;
		virtual RenderScene* getScene() = 0;
		virtual int getWidth() = 0;
		virtual int getHeight() = 0;
		virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;
		virtual void setWireframe(bool wireframe) = 0;
		virtual void renderModel(Model& model, const Matrix& mtx) = 0;
		virtual void toggleStats() = 0;
		virtual void setWindowHandle(void* data) = 0;
};


}
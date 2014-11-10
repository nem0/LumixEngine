#pragma once

#include "core/lumix.h"
#include "core/delegate_list.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"

namespace Lumix
{
	
struct Component;
class FrameBuffer;
class ISerializer;
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
	PipelineManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{}
	~PipelineManager() {}

protected:
	virtual Resource* createResource(const Path& path) override;
	virtual void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


class LUMIX_ENGINE_API Pipeline : public Resource
{
	public:
		Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		virtual ~Pipeline() {}

		virtual bool deserialize(ISerializer& serializer) = 0;

		static Pipeline* create(Renderer& renderer);
		static void destroy(Pipeline* pipeline);
};


class LUMIX_ENGINE_API PipelineInstance abstract
{
	public:
		typedef Delegate<void> CustomCommandHandler; 

	public:
		virtual ~PipelineInstance() {}

		virtual void render() = 0;
		virtual void resize(int w, int h) = 0;
		virtual FrameBuffer* getShadowmapFramebuffer() = 0;

		static PipelineInstance* create(Pipeline& src, IAllocator& allocator);
		static void destroy(PipelineInstance* pipeline);

		virtual Renderer& getRenderer() = 0;
		virtual void setRenderer(Renderer& renderer) = 0;
		virtual void setScene(RenderScene* scene) = 0;
		virtual RenderScene* getScene() = 0;
		virtual CustomCommandHandler& addCustomCommandHandler(const char* name) = 0;

};


}
#pragma once

#include "core/lux.h"
#include "core/delegate_list.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"

namespace Lux
{
	
struct Component;
class ISerializer;
class Renderer;

namespace FS
{

class FileSystem;
class IFile;

}


class LUX_ENGINE_API PipelineManager : public ResourceManagerBase
{
public:
	PipelineManager() : ResourceManagerBase() {}
	~PipelineManager() {}

protected:
	virtual Resource* createResource(const Path& path) LUX_OVERRIDE;
	virtual void destroyResource(Resource& resource) LUX_OVERRIDE;
};


class LUX_ENGINE_API Pipeline : public Resource
{
	public:
		Pipeline(const Path& path, ResourceManager& resource_manager);
		virtual ~Pipeline() {}

		virtual bool deserialize(ISerializer& serializer) = 0;

		virtual DelegateList<void(Pipeline&)>& onLoaded() = 0;
		virtual bool isReady() const = 0;

		static Pipeline* create(Renderer& renderer);
		static void destroy(Pipeline* pipeline);
};


class LUX_ENGINE_API PipelineInstance abstract
{
	public:
		virtual ~PipelineInstance() {}

		virtual void render() = 0;
		virtual const Component& getCamera(int index) = 0;
		virtual void setCamera(int index, const Component& camera) = 0;
		virtual int getCameraCount() const = 0;
		virtual void resize(int w, int h) = 0;

		static PipelineInstance* create(Pipeline& src);
		static void destroy(PipelineInstance* pipeline);
};


}
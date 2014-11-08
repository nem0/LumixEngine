#include "core/lumix.h"
#include "graphics/model_manager.h"

#include "core/resource.h"
#include "graphics/model.h"

namespace Lumix
{
	Resource* ModelManager::createResource(const Path& path)
	{
		return m_allocator.newObject<Model>(path, getOwner());
	}

	void ModelManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<Model*>(&resource));
	}
}
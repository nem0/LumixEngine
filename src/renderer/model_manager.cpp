#include "engine/lumix.h"
#include "renderer/model_manager.h"

#include "engine/resource.h"
#include "renderer/model.h"

namespace Lumix
{
	Resource* ModelManager::createResource(const Path& path)
	{
		return LUMIX_NEW(m_allocator, Model)(path, *this, m_allocator);
	}

	void ModelManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(m_allocator, static_cast<Model*>(&resource));
	}
}
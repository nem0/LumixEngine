#include "engine/lumix.h"
#include "renderer/material_manager.h"

#include "engine/core/resource.h"
#include "renderer/material.h"

namespace Lumix
{
	Resource* MaterialManager::createResource(const Path& path)
	{
		return LUMIX_NEW(m_allocator, Material)(path, getOwner(), m_allocator);
	}

	void MaterialManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(m_allocator, static_cast<Material*>(&resource));
	}
}
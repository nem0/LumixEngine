#include "lumix.h"
#include "renderer/material_manager.h"

#include "core/resource.h"
#include "renderer/material.h"

namespace Lumix
{
	Resource* MaterialManager::createResource(const Path& path)
	{
		return m_allocator.newObject<Material>(path, getOwner(), m_allocator);
	}

	void MaterialManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<Material*>(&resource));
	}
}
#include "core/lumix.h"
#include "graphics/material_manager.h"

#include "core/resource.h"
#include "graphics/material.h"

namespace Lumix
{
	Resource* MaterialManager::createResource(const Path& path)
	{
		return m_allocator.newObject<Material>(path, getOwner());
	}

	void MaterialManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<Material*>(&resource));
	}
}
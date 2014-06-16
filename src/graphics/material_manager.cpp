#include "core/lumix.h"
#include "graphics/material_manager.h"

#include "core/resource.h"
#include "graphics/material.h"

namespace Lumix
{
	Resource* MaterialManager::createResource(const Path& path)
	{
		return LUMIX_NEW(Material)(path, getOwner());
	}

	void MaterialManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Material*>(&resource));
	}
}
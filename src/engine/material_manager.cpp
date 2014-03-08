#include "core/lux.h"
#include "engine/material_manager.h"

#include "core/resource.h"
#include "graphics/material.h"

namespace Lux
{
	Resource* MaterialManager::createResource(const Path& path)
	{
		return LUX_NEW(Material)(path, getOwner());
	}

	void MaterialManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Material*>(&resource));
	}
}
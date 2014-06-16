#include "core/lumix.h"
#include "graphics/model_manager.h"

#include "core/resource.h"
#include "graphics/model.h"

namespace Lumix
{
	Resource* ModelManager::createResource(const Path& path)
	{
		return LUMIX_NEW(Model)(path, getOwner());
	}

	void ModelManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Model*>(&resource));
	}
}
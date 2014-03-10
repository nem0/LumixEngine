#include "core/lux.h"
#include "graphics/model_manager.h"

#include "core/resource.h"
#include "graphics/model.h"

namespace Lux
{
	Resource* ModelManager::createResource(const Path& path)
	{
		return LUX_NEW(Model)(path, getOwner());
	}

	void ModelManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Model*>(&resource));
	}
}
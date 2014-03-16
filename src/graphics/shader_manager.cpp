#include "core/lux.h"
#include "graphics/shader_manager.h"

#include "core/resource.h"
#include "graphics/shader.h"

namespace Lux
{
	Resource* ShaderManager::createResource(const Path& path)
	{
		return LUX_NEW(Shader)(path, getOwner());
	}

	void ShaderManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Shader*>(&resource));
	}
}
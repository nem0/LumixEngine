#include "core/lux.h"
#include "graphics/texture_manager.h"

#include "core/resource.h"
#include "graphics/texture.h"

namespace Lux
{
	Resource* TextureManager::createResource(const Path& path)
	{
		return LUX_NEW(Texture)(path, getOwner());
	}

	void TextureManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Texture*>(&resource));
	}
}
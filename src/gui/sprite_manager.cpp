#include "sprite_manager.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "renderer/renderer.h"


namespace Lumix
{


const ResourceType Sprite::TYPE("sprite");


Sprite::Sprite(const Path& path, ResourceManagerBase& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_texture(nullptr)
{
}


void Sprite::unload()
{
	if (!m_texture) return;
	
	m_texture->getResourceManager().unload(*m_texture);
	m_texture = nullptr;
}


void Sprite::setTexture(const Path& path)
{
	if (m_texture)
	{
		m_texture->getResourceManager().unload(*m_texture);
	}
	if (path.isValid())
	{
		m_texture = (Texture*)getResourceManager().getOwner().get(Texture::TYPE)->load(path);
	}
	else
	{
		m_texture = nullptr;
	}
}


bool Sprite::load(FS::IFile& file)
{
	auto& manager = (SpriteManager&)getResourceManager();
	IAllocator& allocator = manager.m_allocator;
	JsonDeserializer serializer(file, getPath(), allocator);
	serializer.deserializeObjectBegin();
	while (!serializer.isObjectEnd())
	{
		char tmp[32];
		serializer.deserializeLabel(tmp, lengthOf(tmp));
		if (equalIStrings(tmp, "type"))
		{
			serializer.deserialize(tmp, lengthOf(tmp), "");
			type = equalIStrings(tmp, "patch9") ? PATCH9 : SIMPLE;
		}
		else if (equalIStrings(tmp, "top"))
		{
			serializer.deserialize(top, 0);
		}
		else if (equalIStrings(tmp, "bottom"))
		{
			serializer.deserialize(bottom, 0);
		}
		else if (equalIStrings(tmp, "left"))
		{
			serializer.deserialize(left, 0);
		}
		else if (equalIStrings(tmp, "right"))
		{
			serializer.deserialize(right, 0);
		}
		else if (equalIStrings(tmp, "texture"))
		{
			char texture_path[MAX_PATH_LENGTH];
			serializer.deserialize(texture_path, lengthOf(texture_path), "");
			auto* mng = m_resource_manager.getOwner().get(Texture::TYPE);
			m_texture = texture_path[0] != '\0' ? (Texture*)mng->load(Path(texture_path)) : nullptr;
		}
		else
		{
			g_log_error.log("gui") << "Unknown label " << tmp << " in " << getPath();
		}
	}
	return true;
}


SpriteManager::SpriteManager(IAllocator& allocator)
	: ResourceManagerBase(allocator)
	, m_allocator(allocator)
{
}


Resource* SpriteManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, Sprite)(path, *this, m_allocator);
}


void SpriteManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<Sprite*>(&resource));
}


} // namespace Lumix
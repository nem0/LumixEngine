#include "sprite_manager.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/texture.h"
#include "renderer/renderer.h"


namespace Lumix
{


const ResourceType Sprite::TYPE("sprite");


Sprite::Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator)
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
		m_texture = (Texture*)getResourceManager().getOwner().load<Texture>(path);
	}
	else
	{
		m_texture = nullptr;
	}
}


bool Sprite::save(JsonSerializer& serializer)
{
	if (!isReady()) return false;

	serializer.beginObject();
	serializer.serialize("type", type == PATCH9 ? "patch9" : "simple");
	serializer.serialize("top", top);
	serializer.serialize("bottom", bottom);
	serializer.serialize("left", left);
	serializer.serialize("right", right);
	serializer.serialize("texture", m_texture ? m_texture->getPath().c_str() : "");
	serializer.endObject();

	return true;
}


bool Sprite::load(u64 size, const u8* mem)
{
	auto& manager = (SpriteManager&)getResourceManager();
	IAllocator& allocator = manager.m_allocator;
	InputMemoryStream file(mem, size);
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
			ResourceManagerHub& mng = m_resource_manager.getOwner();
			m_texture = texture_path[0] != '\0' ? mng.load<Texture>(Path(texture_path)) : nullptr;
		}
		else
		{
			g_log_error.log("gui") << "Unknown label " << tmp << " in " << getPath();
		}
	}
	return true;
}


SpriteManager::SpriteManager(IAllocator& allocator)
	: ResourceManager(allocator)
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
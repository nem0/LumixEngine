#include "sprite.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "engine/stream.h"
#include "renderer/texture.h"


namespace Lumix
{


const ResourceType Sprite::TYPE("sprite");


Sprite::Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_allocator(allocator)
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


bool Sprite::save(TextSerializer& serializer)
{
	if (!isReady()) return false;

	serializer.write("type", type == PATCH9 ? "patch9" : "simple");
	serializer.write("top", top);
	serializer.write("bottom", bottom);
	serializer.write("left", left);
	serializer.write("right", right);
	serializer.write("texture", m_texture ? m_texture->getPath().c_str() : "");

	return true;
}


bool Sprite::load(u64 size, const u8* mem)
{
	InputMemoryStream file(mem, size);
	struct : ILoadEntityGUIDMap {
		EntityPtr get(EntityGUID guid) override { ASSERT(false); return INVALID_ENTITY; }
	} dummy_map;
	TextDeserializer serializer(file, dummy_map);
	char tmp[MAX_PATH_LENGTH];
	serializer.read(tmp, lengthOf(tmp));
	type = equalStrings(tmp, "simple") ? SIMPLE : PATCH9; 
	serializer.read(Ref(top));
	serializer.read(Ref(bottom));
	serializer.read(Ref(left));
	serializer.read(Ref(right));
	serializer.read(tmp, lengthOf(tmp));
	ResourceManagerHub& mng = m_resource_manager.getOwner();
	m_texture = tmp[0] != '\0' ? mng.load<Texture>(Path(tmp)) : nullptr;
	return true;
}


} // namespace Lumix
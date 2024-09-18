#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
#include "core/tokenizer.h"
#include "engine/resource_manager.h"
#include "renderer/texture.h"
#include "sprite.h"

namespace Lumix {

const ResourceType Sprite::TYPE("sprite");

Sprite::Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_texture(nullptr)
{}


void Sprite::unload() {
	if (!m_texture) return;
	
	m_texture->decRefCount();
	m_texture = nullptr;
}


void Sprite::setTexture(const Path& path) {
	if (m_texture) {
		m_texture->decRefCount();
	}

	if (path.isEmpty()) {
		m_texture = nullptr;
	} else {
		m_texture = getResourceManager().getOwner().load<Texture>(path);
	}
}


void Sprite::serialize(OutputMemoryStream& out) {
	ASSERT(isReady());
	out << "type " << (type == PATCH9 ? "\"patch9\"\n" : "\"simple\"\n");
	out << "top " << top << "\n";
	out << "bottom " << bottom << "\n";
	out << "left " << left << "\n";
	out << "right " << right << "\n";
	if (m_texture) {
		out << "texture \"/" << m_texture->getPath() << "\"";
	} else {
		out << "texture \"\"";
	}
}

bool Sprite::load(Span<const u8> mem) {
	StringView type_str, texture_str;
	const ParseItemDesc descs[] = {
		{"type", &type_str},
		{"top", &top},
		{"bottom", &bottom},
		{"left", &left},
		{"right", &right},
		{"texture", &texture_str}
	};
	
	if (!parse(mem, getPath().c_str(), descs)) return false;
	
	type = equalIStrings(type_str, "patch9") ? PATCH9 : SIMPLE;
	setTexture(Path(texture_str));
	return true;
}


} // namespace Lumix
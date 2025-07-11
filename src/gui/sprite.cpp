#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
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

bool Sprite::load(Span<const u8> mem) {
	InputMemoryStream stream(mem);
	Header header;
	stream.read(header);
	if (header.magic != Header::MAGIC) {
		logError(getPath(), ": invalid file");
		return false;
	}
	if (header.version != 0) {
		logError(getPath(), ": unsupported version");
		return false;
	}

	stream.read(top);
	stream.read(bottom);
	stream.read(left);
	stream.read(right);
	const char* texture = stream.readString();
	StringView dir = Path::getDir(getPath());
	StringView tex_dir = Path::getDir(texture);
	if (tex_dir.empty()) {
		setTexture(Path(dir, "/", texture));
	}
	else {
		setTexture(Path(texture));
	}
	type = stream.read<Type>();
	return !stream.hasOverflow();
}


} // namespace Lumix
#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/resource_manager.h"
#include "renderer/draw2d.h"
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

void Sprite::render(Draw2D& draw, float l, float t, float r, float b, Color color) {
	Texture* tex = getTexture();
	if (!tex) return;

	if (type == PATCH9) {
		struct Quad {
			float l, t, r, b;
		} pos = {
			l + left,
			t + top,
			r - tex->width + right,
			b - tex->height + bottom
		};
		if (pos.l > pos.r) {
			pos.l = pos.r = (pos.l + pos.r) * 0.5f;
		}
		if (pos.t > pos.b) {
			pos.t = pos.b = (pos.t + pos.b) * 0.5f;
		}
		Quad uvs = {
			left / (float)tex->width,
			top / (float)tex->height,
			right / (float)tex->width,
			bottom / (float)tex->height
		};

		draw.addImage(tex->handle, { l, t }, { pos.l, pos.t }, { 0, 0 }, { uvs.l, uvs.t }, color);
		draw.addImage(tex->handle, { pos.l, t }, { pos.r, pos.t }, { uvs.l, 0 }, { uvs.r, uvs.t }, color);
		draw.addImage(tex->handle, { pos.r, t }, { r, pos.t }, { uvs.r, 0 }, { 1, uvs.t }, color);

		draw.addImage(tex->handle, { l, pos.t }, { pos.l, pos.b }, { 0, uvs.t }, { uvs.l, uvs.b }, color);
		draw.addImage(tex->handle, { pos.l, pos.t }, { pos.r, pos.b }, { uvs.l, uvs.t }, { uvs.r, uvs.b }, color);
		draw.addImage(tex->handle, { pos.r, pos.t }, { r, pos.b }, { uvs.r, uvs.t }, { 1, uvs.b }, color);

		draw.addImage(tex->handle, { l, pos.b }, { pos.l, b }, { 0, uvs.b }, { uvs.l, 1 }, color);
		draw.addImage(tex->handle, { pos.l, pos.b }, { pos.r, b }, { uvs.l, uvs.b }, { uvs.r, 1 }, color);
		draw.addImage(tex->handle, { pos.r, pos.b }, { r, b }, { uvs.r, uvs.b }, { 1, 1 }, color);
	} else {
		draw.addImage(tex->handle, { l, t }, { r, b }, {0, 0}, {1, 1}, color);
	}
}


} // namespace Lumix
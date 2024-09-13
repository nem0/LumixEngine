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
	Tokenizer tokenizer(StringView((const char*)mem.begin(), (u32)mem.length()), getPath().c_str());

	for (;;) {
		Tokenizer::Token token = tokenizer.tryNextToken();
		switch (token.type) {
			case Tokenizer::Token::ERROR: return false;
			case Tokenizer::Token::EOF: return true;
			default: break;
		}

		StringView value;
		if (token == "type") {
			if (!tokenizer.consume(value)) return false;
			type = equalIStrings(value, "patch9") ? PATCH9 : SIMPLE;
		}
		else if (token == "top") {
			if (!tokenizer.consume(top)) return false;
		}
		else if (token == "bottom") {
			if (!tokenizer.consume(bottom)) return false;
		}
		else if (token == "left") {
			if (!tokenizer.consume(left)) return false;
		}
		else if (token == "right") {
			if (!tokenizer.consume(right)) return false;
		}
		else if (token == "texture") {
			if (!tokenizer.consume(value)) return false;
			setTexture(Path(value));
		}
		else {
			logError(getPath(), "(", tokenizer.getLine(), "): Unknown token ", token.value);
			return false;
		}
	}

	return true;
}


} // namespace Lumix
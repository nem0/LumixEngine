#pragma once


#include "engine/resource.h"


namespace Lumix
{

struct Sprite final : Resource {
	struct Header {
		static const u32 MAGIC = '_SPR';
		u32 magic = MAGIC;
		u32 version = 0;
	};

	enum Type : u8 {
		PATCH9,
		SIMPLE
	};

	Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	
	void setTexture(const Path& path);
	struct Texture* getTexture() const { return m_texture; }

	Type type = SIMPLE;
	i32 top = 0;
	i32 bottom = 0;
	i32 left = 0;
	i32 right = 0;

	static const ResourceType TYPE;

private:
	Texture* m_texture;
};


} // namespace Lumix
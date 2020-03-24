#pragma once


#include "engine/resource.h"


namespace Lumix
{

struct Sprite final : Resource
{
public:
	enum Type
	{
		PATCH9,
		SIMPLE
	};

	Sprite(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(u64 size, const u8* mem) override;
	bool save(struct IOutputStream& serializer);
	
	void setTexture(const Path& path);
	struct Texture* getTexture() const { return m_texture; }

	Type type = SIMPLE;
	int top = 0;
	int bottom = 0;
	int left = 0;
	int right = 0;

	static const ResourceType TYPE;

private:
	Texture* m_texture;
};


} // namespace Lumix
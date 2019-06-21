#pragma once


#include "engine/resource.h"


namespace Lumix
{


class Renderer;
struct TextSerializer;
class Texture;


class Sprite final : public Resource
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
	bool save(TextSerializer& serializer);
	
	void setTexture(const Path& path);
	Texture* getTexture() const { return m_texture; }

	Type type;
	int top;
	int bottom;
	int left;
	int right;

	static const ResourceType TYPE;

private:
	IAllocator& m_allocator;
	Texture* m_texture;
};


} // namespace Lumix
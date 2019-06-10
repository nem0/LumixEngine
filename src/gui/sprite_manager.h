#pragma once


#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix
{


class JsonSerializer;
class Renderer;
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
	bool load(u64 size, void* mem) override;
	bool save(JsonSerializer& serializer);
	
	void setTexture(const Path& path);
	Texture* getTexture() const { return m_texture; }

	Type type;
	int top;
	int bottom;
	int left;
	int right;

	static const ResourceType TYPE;

private:
	Texture* m_texture;
};


class SpriteManager final : public ResourceManager
{
friend class Sprite;
public:
	SpriteManager(IAllocator& allocator);

private:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


} // namespace Lumix
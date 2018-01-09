#pragma once


#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


class Renderer;
class Texture;


class LUMIX_RENDERER_API Sprite LUMIX_FINAL : public Resource
{
public:
	enum Type
	{
		PATCH9,
		SIMPLE
	};

	Sprite(const Path& path, ResourceManagerBase& manager, IAllocator& allocator);

	ResourceType getType() const override { return ResourceType("sprite"); }

	void unload() override;
	bool load(FS::IFile& file) override;
	
	void setTexture(const Path& path);
	Texture* getTexture() const { return m_texture; }

	Type type;
	int top;
	int bottom;
	int left;
	int right;

private:
	Texture* m_texture;
};


class LUMIX_RENDERER_API SpriteManager LUMIX_FINAL : public ResourceManagerBase
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
#pragma once


#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


class Renderer;


class FontResource LUMIX_FINAL : public Resource
{
public:
	FontResource(const Path& path, ResourceManagerBase& manager, IAllocator& allocator)
		: Resource(path, manager, allocator)
	{
	}

	ResourceType getType() const override { return ResourceType("font"); }

	void unload() override {}
	bool load(FS::IFile& file) override { return true; }
};


class LUMIX_RENDERER_API FontManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	FontManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{}
	~FontManager() {}

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


} // namespace Lumix
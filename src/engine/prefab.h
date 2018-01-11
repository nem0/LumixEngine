#pragma once


#include "engine/blob.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


enum class PrefabVersion : u32
{
	FIRST,
	WITH_HIERARCHY,

	LAST
};


struct PrefabResource LUMIX_FINAL : public Resource
{
	PrefabResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, blob(allocator)
	{
	}


	ResourceType getType() const override { return TYPE; }


	void unload() override { blob.clear(); }


	bool load(FS::IFile& file) override
	{
		file.getContents(blob);
		return true;
	}


	OutputBlob blob;


	static const ResourceType TYPE;
};


class PrefabResourceManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	explicit PrefabResourceManager(IAllocator& allocator)
		: m_allocator(allocator)
		, ResourceManagerBase(allocator)
	{
	}


protected:
	Resource* createResource(const Path& path) override
	{
		return LUMIX_NEW(m_allocator, PrefabResource)(path, *this, m_allocator);
	}


	void destroyResource(Resource& resource) override
	{
		return LUMIX_DELETE(m_allocator, &static_cast<PrefabResource&>(resource));
	}


private:
	IAllocator& m_allocator;
};


} // namespace Lumix
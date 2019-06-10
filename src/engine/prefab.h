#pragma once


#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"


namespace Lumix
{


enum class PrefabVersion : u32
{
	FIRST,
	WITH_HIERARCHY,

	LAST
};


struct LUMIX_ENGINE_API PrefabResource final : public Resource
{
	PrefabResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, data(allocator)
	{
	}


	ResourceType getType() const override { return TYPE; }


	void unload() override { data.clear(); }


	bool load(u64 size, const u8* mem) override
	{
		data.resize((int)size);
		copyMemory(data.begin(), mem, size);
		return true;
	}


	Array<u8> data;
	static const ResourceType TYPE;
};


class PrefabResourceManager final : public ResourceManager
{
public:
	explicit PrefabResourceManager(IAllocator& allocator)
		: m_allocator(allocator)
		, ResourceManager(allocator)
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
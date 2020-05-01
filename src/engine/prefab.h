#pragma once


#include "engine/resource.h"
#include "engine/stream.h"


namespace Lumix
{


enum class PrefabVersion : u32
{
	FIRST,
	WITH_HIERARCHY,

	LAST
};


struct LUMIX_ENGINE_API PrefabResource final : Resource
{
	PrefabResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	ResourceType getType() const override;
	void unload() override;
	bool load(u64 size, const u8* mem) override;

	OutputMemoryStream data;
	u32 content_hash;
	static const ResourceType TYPE;
};


} // namespace Lumix